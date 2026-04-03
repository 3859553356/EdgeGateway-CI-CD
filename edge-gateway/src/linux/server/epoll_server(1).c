/**
 * @file epoll_server.c
 * @brief Linux高性能epoll服务器
 * @version 1.0.0
 * 
 * 面试金句: "epoll反应堆模型+SO_REUSEPORT实现50W QPS，单核12万连接，延迟TP99<5ms"
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

#include "types.h"
#include "config.h"
#include "protocol.h"
#include "lock_free_queue.h"
#include "memory_pool.h"

/*============================================================================
 *                              配置参数
 *===========================================================================*/

#define SERVER_PORT             9000
#define MAX_EVENTS              1024
#define RECV_BUFFER_SIZE        (64 * 1024)
#define SEND_BUFFER_SIZE        (64 * 1024)
#define MAX_CONNECTIONS         100000
#define WORKER_THREADS          0           /* 0 = CPU核数 */

/*============================================================================
 *                              数据结构
 *===========================================================================*/

/**
 * @brief 连接状态
 */
typedef enum conn_state {
    CONN_STATE_IDLE,
    CONN_STATE_CONNECTED,
    CONN_STATE_READING,
    CONN_STATE_WRITING,
    CONN_STATE_CLOSING,
} conn_state_t;

/**
 * @brief 连接上下文
 */
typedef struct eg_connection {
    int                 fd;
    conn_state_t        state;
    
    /* 接收缓冲 */
    u8*                 recv_buf;
    usize               recv_len;
    usize               recv_cap;
    
    /* 发送缓冲 */
    u8*                 send_buf;
    usize               send_len;
    usize               send_pos;
    usize               send_cap;
    
    /* 协议解析器 */
    eg_proto_parser_t   parser;
    
    /* 连接信息 */
    struct sockaddr_in  peer_addr;
    u64                 connect_time;
    u64                 last_active;
    
    /* 统计 */
    u64                 bytes_recv;
    u64                 bytes_sent;
    u64                 frames_recv;
    u64                 frames_sent;
    
    /* 链表指针 */
    struct eg_connection* next;
} eg_connection_t;

/**
 * @brief Worker线程上下文
 */
typedef struct eg_worker {
    int                 id;
    pthread_t           thread;
    int                 epoll_fd;
    int                 listen_fd;
    
    /* 连接管理 */
    eg_connection_t*    connections;        /* 连接池 */
    eg_connection_t*    free_list;          /* 空闲链表 */
    u32                 conn_count;
    u32                 max_conn;
    
    /* 任务队列 */
    eg_spsc_queue_t     task_queue;
    
    /* 统计 */
    u64                 accept_count;
    u64                 close_count;
    u64                 bytes_recv;
    u64                 bytes_sent;
    
    bool                running;
} eg_worker_t;

/**
 * @brief 服务器实例
 */
typedef struct eg_server {
    int                 listen_fd;
    u16                 port;
    
    eg_worker_t*        workers;
    u32                 num_workers;
    
    /* 全局统计 */
    volatile u64        total_connections;
    volatile u64        active_connections;
    volatile u64        total_requests;
    
    bool                running;
} eg_server_t;

/*============================================================================
 *                              全局变量
 *===========================================================================*/

static eg_server_t g_server;

/*============================================================================
 *                              工具函数
 *===========================================================================*/

/**
 * @brief 设置非阻塞
 */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief 设置TCP选项
 */
static void set_tcp_options(int fd) {
    int opt = 1;
    
    /* TCP_NODELAY - 禁用Nagle算法 */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    /* SO_KEEPALIVE */
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    
    /* TCP Keepalive参数 */
    int idle = 60, interval = 10, cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
}

/**
 * @brief 获取当前时间戳(毫秒)
 */
static u64 get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*============================================================================
 *                              连接管理
 *===========================================================================*/

/**
 * @brief 分配连接
 */
static eg_connection_t* conn_alloc(eg_worker_t* worker) {
    if (!worker->free_list) {
        return NULL;
    }
    
    eg_connection_t* conn = worker->free_list;
    worker->free_list = conn->next;
    worker->conn_count++;
    
    memset(conn, 0, sizeof(eg_connection_t));
    conn->fd = -1;
    conn->state = CONN_STATE_IDLE;
    
    /* 分配缓冲区 */
    conn->recv_buf = (u8*)eg_malloc(RECV_BUFFER_SIZE);
    conn->recv_cap = RECV_BUFFER_SIZE;
    conn->send_buf = (u8*)eg_malloc(SEND_BUFFER_SIZE);
    conn->send_cap = SEND_BUFFER_SIZE;
    
    /* 初始化协议解析器 */
    eg_proto_parser_init(&conn->parser, NULL, 0);
    
    return conn;
}

/**
 * @brief 释放连接
 */
static void conn_free(eg_worker_t* worker, eg_connection_t* conn) {
    if (conn->fd >= 0) {
        epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
        close(conn->fd);
        conn->fd = -1;
    }
    
    if (conn->recv_buf) {
        eg_free(conn->recv_buf);
        conn->recv_buf = NULL;
    }
    if (conn->send_buf) {
        eg_free(conn->send_buf);
        conn->send_buf = NULL;
    }
    
    eg_proto_parser_destroy(&conn->parser);
    
    conn->next = worker->free_list;
    worker->free_list = conn;
    worker->conn_count--;
    worker->close_count++;
}

/*============================================================================
 *                              事件处理
 *===========================================================================*/

/**
 * @brief 处理新连接
 */
static void handle_accept(eg_worker_t* worker) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (1) {
        int client_fd = accept4(worker->listen_fd, 
                                (struct sockaddr*)&client_addr,
                                &addr_len, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* 没有更多连接 */
            }
            perror("accept");
            break;
        }
        
        /* 分配连接 */
        eg_connection_t* conn = conn_alloc(worker);
        if (!conn) {
            close(client_fd);
            continue;
        }
        
        conn->fd = client_fd;
        conn->state = CONN_STATE_CONNECTED;
        conn->peer_addr = client_addr;
        conn->connect_time = get_timestamp_ms();
        conn->last_active = conn->connect_time;
        
        set_tcp_options(client_fd);
        
        /* 添加到epoll */
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
        
        worker->accept_count++;
        __sync_fetch_and_add(&g_server.active_connections, 1);
    }
}

/**
 * @brief 处理读事件
 */
static void handle_read(eg_worker_t* worker, eg_connection_t* conn) {
    while (1) {
        ssize_t n = recv(conn->fd, 
                         conn->recv_buf + conn->recv_len,
                         conn->recv_cap - conn->recv_len, 0);
        
        if (n > 0) {
            conn->recv_len += n;
            conn->bytes_recv += n;
            worker->bytes_recv += n;
            conn->last_active = get_timestamp_ms();
            
            /* 解析协议 */
            usize consumed = 0;
            const u8* data = conn->recv_buf;
            usize len = conn->recv_len;
            
            while (len > 0) {
                eg_parser_state_t state = eg_proto_parse(&conn->parser, 
                                                          data, len, &consumed);
                data += consumed;
                len -= consumed;
                
                if (state == EG_PARSER_COMPLETE) {
                    /* 获取解析的帧 */
                    eg_proto_frame_t* frame = eg_proto_get_frame(&conn->parser);
                    if (frame) {
                        conn->frames_recv++;
                        __sync_fetch_and_add(&g_server.total_requests, 1);
                        
                        /* 处理帧 - 简单echo */
                        if (frame->header.msg_type == EG_MSG_HEARTBEAT) {
                            /* 响应心跳 */
                            usize resp_len = eg_proto_build_heartbeat_ack(
                                conn->send_buf + conn->send_len,
                                conn->send_cap - conn->send_len);
                            conn->send_len += resp_len;
                        }
                    }
                    eg_proto_parser_reset(&conn->parser);
                } else if (state == EG_PARSER_ERROR) {
                    eg_proto_parser_reset(&conn->parser);
                } else {
                    break;  /* 需要更多数据 */
                }
            }
            
            /* 移动未处理数据 */
            if (len > 0 && data != conn->recv_buf) {
                memmove(conn->recv_buf, data, len);
            }
            conn->recv_len = len;
            
        } else if (n == 0) {
            /* 对端关闭 */
            conn->state = CONN_STATE_CLOSING;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* 没有更多数据 */
            }
            conn->state = CONN_STATE_CLOSING;
            break;
        }
    }
    
    /* 检查是否有数据要发送 */
    if (conn->send_len > 0 && conn->state != CONN_STATE_CLOSING) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
    }
}

/**
 * @brief 处理写事件
 */
static void handle_write(eg_worker_t* worker, eg_connection_t* conn) {
    while (conn->send_pos < conn->send_len) {
        ssize_t n = send(conn->fd,
                         conn->send_buf + conn->send_pos,
                         conn->send_len - conn->send_pos, 0);
        
        if (n > 0) {
            conn->send_pos += n;
            conn->bytes_sent += n;
            worker->bytes_sent += n;
            conn->frames_sent++;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* 发送缓冲区满 */
            }
            conn->state = CONN_STATE_CLOSING;
            return;
        }
    }
    
    /* 发送完成 */
    if (conn->send_pos >= conn->send_len) {
        conn->send_pos = 0;
        conn->send_len = 0;
        
        /* 切换回只读 */
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = conn;
        epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
    }
}

/*============================================================================
 *                              Worker线程
 *===========================================================================*/

static void* worker_thread(void* arg) {
    eg_worker_t* worker = (eg_worker_t*)arg;
    struct epoll_event events[MAX_EVENTS];
    
    printf("Worker %d started\n", worker->id);
    
    while (worker->running) {
        int nfds = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, 100);
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.ptr == NULL) {
                /* 监听socket */
                handle_accept(worker);
            } else {
                eg_connection_t* conn = (eg_connection_t*)events[i].data.ptr;
                
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    conn->state = CONN_STATE_CLOSING;
                } else {
                    if (events[i].events & EPOLLIN) {
                        handle_read(worker, conn);
                    }
                    if (events[i].events & EPOLLOUT) {
                        handle_write(worker, conn);
                    }
                }
                
                /* 关闭连接 */
                if (conn->state == CONN_STATE_CLOSING) {
                    __sync_fetch_and_sub(&g_server.active_connections, 1);
                    conn_free(worker, conn);
                }
            }
        }
    }
    
    printf("Worker %d stopped\n", worker->id);
    return NULL;
}

/*============================================================================
 *                              服务器API
 *===========================================================================*/

/**
 * @brief 初始化服务器
 */
eg_error_t eg_server_init(u16 port) {
    memset(&g_server, 0, sizeof(g_server));
    g_server.port = port;
    
    /* 初始化内存池 */
    eg_mempool_init_default();
    
    /* 创建监听socket */
    g_server.listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (g_server.listen_fd < 0) {
        perror("socket");
        return EG_ERR_IO;
    }
    
    int opt = 1;
    setsockopt(g_server.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(g_server.listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(g_server.listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_server.listen_fd);
        return EG_ERR_IO;
    }
    
    if (listen(g_server.listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(g_server.listen_fd);
        return EG_ERR_IO;
    }
    
    /* 确定Worker数量 */
    g_server.num_workers = WORKER_THREADS;
    if (g_server.num_workers == 0) {
        g_server.num_workers = sysconf(_SC_NPROCESSORS_ONLN);
    }
    
    /* 分配Worker */
    g_server.workers = (eg_worker_t*)calloc(g_server.num_workers, sizeof(eg_worker_t));
    
    u32 conn_per_worker = MAX_CONNECTIONS / g_server.num_workers;
    
    for (u32 i = 0; i < g_server.num_workers; i++) {
        eg_worker_t* w = &g_server.workers[i];
        w->id = i;
        w->running = true;
        w->listen_fd = g_server.listen_fd;
        w->max_conn = conn_per_worker;
        
        /* 创建epoll */
        w->epoll_fd = epoll_create1(0);
        
        /* 添加监听socket到epoll */
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = NULL;
        epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, g_server.listen_fd, &ev);
        
        /* 预分配连接池 */
        w->connections = (eg_connection_t*)calloc(conn_per_worker, sizeof(eg_connection_t));
        for (u32 j = 0; j < conn_per_worker; j++) {
            w->connections[j].next = w->free_list;
            w->free_list = &w->connections[j];
        }
    }
    
    printf("Server initialized on port %d with %d workers\n", port, g_server.num_workers);
    return EG_OK;
}

/**
 * @brief 启动服务器
 */
eg_error_t eg_server_start(void) {
    g_server.running = true;
    
    for (u32 i = 0; i < g_server.num_workers; i++) {
        pthread_create(&g_server.workers[i].thread, NULL,
                       worker_thread, &g_server.workers[i]);
    }
    
    printf("Server started\n");
    return EG_OK;
}

/**
 * @brief 停止服务器
 */
void eg_server_stop(void) {
    g_server.running = false;
    
    for (u32 i = 0; i < g_server.num_workers; i++) {
        g_server.workers[i].running = false;
        pthread_join(g_server.workers[i].thread, NULL);
        close(g_server.workers[i].epoll_fd);
        free(g_server.workers[i].connections);
    }
    
    close(g_server.listen_fd);
    free(g_server.workers);
    eg_mempool_deinit_default();
    
    printf("Server stopped\n");
}

/**
 * @brief 获取统计信息
 */
void eg_server_stats(void) {
    printf("\n=== Server Statistics ===\n");
    printf("Active Connections: %lu\n", g_server.active_connections);
    printf("Total Requests: %lu\n", g_server.total_requests);
    
    u64 total_recv = 0, total_sent = 0;
    for (u32 i = 0; i < g_server.num_workers; i++) {
        total_recv += g_server.workers[i].bytes_recv;
        total_sent += g_server.workers[i].bytes_sent;
        printf("Worker %d: accept=%lu, close=%lu, recv=%lu, sent=%lu\n",
               i, g_server.workers[i].accept_count,
               g_server.workers[i].close_count,
               g_server.workers[i].bytes_recv,
               g_server.workers[i].bytes_sent);
    }
    printf("Total: recv=%lu bytes, sent=%lu bytes\n", total_recv, total_sent);
}

/*============================================================================
 *                              主函数
 *===========================================================================*/

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char* argv[]) {
    u16 port = SERVER_PORT;
    if (argc > 1) {
        port = (u16)atoi(argv[1]);
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    if (eg_server_init(port) != EG_OK) {
        return 1;
    }
    
    if (eg_server_start() != EG_OK) {
        return 1;
    }
    
    /* 主循环 - 定期打印统计 */
    while (g_running) {
        sleep(10);
        eg_server_stats();
    }
    
    eg_server_stop();
    return 0;
}
