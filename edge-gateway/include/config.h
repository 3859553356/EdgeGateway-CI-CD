/**
 * @file config.h
 * @brief 全局配置 - 双平台编译开关
 * @version 1.0.0
 * 
 * 面试金句: "通过条件编译实现单一代码库支持ARM Cortex-M7和x86_64，编译时自动选择优化路径"
 */

#ifndef EDGE_GATEWAY_CONFIG_H
#define EDGE_GATEWAY_CONFIG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *                              版本配置
 *===========================================================================*/

#define EG_PROJECT_NAME         "EdgeGateway"
#define EG_VERSION_MAJOR        1
#define EG_VERSION_MINOR        0
#define EG_VERSION_PATCH        0
#define EG_BUILD_DATE           __DATE__
#define EG_BUILD_TIME           __TIME__

/*============================================================================
 *                              平台配置
 *===========================================================================*/

/* 自动检测或手动指定 */
#if !defined(EG_PLATFORM_EMBEDDED) && !defined(EG_PLATFORM_LINUX)
    #if defined(__arm__) || defined(__thumb__)
        #define EG_PLATFORM_EMBEDDED    1
    #else
        #define EG_PLATFORM_LINUX       1
    #endif
#endif

/*============================================================================
 *                              内存配置
 *===========================================================================*/

#if defined(EG_PLATFORM_EMBEDDED)
    /* STM32H7: 1MB RAM (DTCM 128KB + AXI SRAM 512KB + SRAM 256KB) */
    #define EG_MEM_POOL_SIZE        (256 * 1024)    /* 主内存池 256KB */
    #define EG_MEM_BLOCK_SIZES      {32, 64, 128, 256, 512, 1024, 2048}
    #define EG_MEM_BLOCK_COUNTS     {256, 128, 64, 32, 16, 8, 4}
    #define EG_MEM_ALIGNMENT        8
    
    /* DMA缓冲区 (需要在特定SRAM) */
    #define EG_DMA_BUFFER_SIZE      (16 * 1024)     /* 16KB DMA缓冲 */
    
#else /* EG_PLATFORM_LINUX */
    /* Linux: 使用jemalloc + 对象池 */
    #define EG_MEM_POOL_SIZE        (64 * 1024 * 1024)  /* 64MB */
    #define EG_MEM_BLOCK_SIZES      {64, 128, 256, 512, 1024, 2048, 4096, 8192}
    #define EG_MEM_BLOCK_COUNTS     {65536, 32768, 16384, 8192, 4096, 2048, 1024, 512}
    #define EG_MEM_ALIGNMENT        64  /* 缓存行对齐 */
    
    /* 大页支持 */
    #define EG_USE_HUGE_PAGES       1
    #define EG_HUGE_PAGE_SIZE       (2 * 1024 * 1024)
#endif

/*============================================================================
 *                              队列配置
 *===========================================================================*/

#if defined(EG_PLATFORM_EMBEDDED)
    #define EG_QUEUE_DEFAULT_SIZE   256
    #define EG_QUEUE_MAX_SIZE       1024
#else
    #define EG_QUEUE_DEFAULT_SIZE   65536
    #define EG_QUEUE_MAX_SIZE       (1024 * 1024)
#endif

/*============================================================================
 *                              网络配置
 *===========================================================================*/

#if defined(EG_PLATFORM_EMBEDDED)
    /* 嵌入式网络配置 */
    #define EG_NET_MAX_CONNECTIONS  16
    #define EG_NET_BUFFER_SIZE      2048
    #define EG_NET_TIMEOUT_MS       5000
    
#else /* EG_PLATFORM_LINUX */
    /* Linux高性能网络 */
    #define EG_NET_MAX_CONNECTIONS  100000
    #define EG_NET_BUFFER_SIZE      (64 * 1024)
    #define EG_NET_TIMEOUT_MS       30000
    
    /* epoll配置 */
    #define EG_EPOLL_MAX_EVENTS     1024
    #define EG_EPOLL_TIMEOUT_MS     100
    
    /* SO_REUSEPORT支持 */
    #define EG_USE_REUSEPORT        1
    
    /* TCP优化 */
    #define EG_TCP_NODELAY          1
    #define EG_TCP_KEEPALIVE        1
    #define EG_TCP_KEEPIDLE         60
    #define EG_TCP_KEEPINTVL        10
    #define EG_TCP_KEEPCNT          3
#endif

/*============================================================================
 *                              协议配置
 *===========================================================================*/

/* 协议帧格式 */
#define EG_PROTO_MAGIC          0xEDGE
#define EG_PROTO_VERSION        0x01
#define EG_PROTO_HEADER_SIZE    8
#define EG_PROTO_MAX_PAYLOAD    (4 * 1024)
#define EG_PROTO_CRC_SIZE       4

/* Modbus配置 */
#define EG_MODBUS_MAX_REGISTERS 125
#define EG_MODBUS_TIMEOUT_MS    1000
#define EG_MODBUS_RETRY_COUNT   3

/*============================================================================
 *                              任务/线程配置
 *===========================================================================*/

#if defined(EG_PLATFORM_EMBEDDED)
    /* FreeRTOS任务配置 */
    #define EG_TASK_STACK_SIZE_MIN      256     /* 最小栈 1KB */
    #define EG_TASK_STACK_SIZE_DEFAULT  512     /* 默认栈 2KB */
    #define EG_TASK_STACK_SIZE_LARGE    1024    /* 大栈 4KB */
    
    #define EG_TASK_PRIORITY_IDLE       1
    #define EG_TASK_PRIORITY_LOW        2
    #define EG_TASK_PRIORITY_NORMAL     3
    #define EG_TASK_PRIORITY_HIGH       5
    #define EG_TASK_PRIORITY_REALTIME   6
    
    /* 采集周期 */
    #define EG_COLLECT_PERIOD_MS        10
    
#else /* EG_PLATFORM_LINUX */
    /* 线程池配置 */
    #define EG_WORKER_THREAD_COUNT      0       /* 0=自动(CPU核数) */
    #define EG_WORKER_QUEUE_SIZE        65536
    
    /* 线程栈大小 */
    #define EG_THREAD_STACK_SIZE        (2 * 1024 * 1024)
#endif

/*============================================================================
 *                              日志配置
 *===========================================================================*/

typedef enum {
    EG_LOG_TRACE = 0,
    EG_LOG_DEBUG = 1,
    EG_LOG_INFO  = 2,
    EG_LOG_WARN  = 3,
    EG_LOG_ERROR = 4,
    EG_LOG_FATAL = 5,
    EG_LOG_OFF   = 6,
} eg_log_level_t;

#if defined(EG_PLATFORM_EMBEDDED)
    #define EG_LOG_DEFAULT_LEVEL    EG_LOG_INFO
    #define EG_LOG_BUFFER_SIZE      512
    #define EG_LOG_USE_COLOR        0
#else
    #define EG_LOG_DEFAULT_LEVEL    EG_LOG_DEBUG
    #define EG_LOG_BUFFER_SIZE      4096
    #define EG_LOG_USE_COLOR        1
    #define EG_LOG_FILE_MAX_SIZE    (100 * 1024 * 1024)  /* 100MB */
    #define EG_LOG_FILE_MAX_COUNT   10
#endif

/*============================================================================
 *                              存储配置
 *===========================================================================*/

#if defined(EG_PLATFORM_EMBEDDED)
    /* SPI Flash配置 */
    #define EG_FLASH_SECTOR_SIZE    4096
    #define EG_FLASH_PAGE_SIZE      256
    #define EG_FLASH_TOTAL_SIZE     (16 * 1024 * 1024)  /* 16MB */
    
    /* 数据缓存配置 */
    #define EG_DATA_CACHE_SIZE      10000   /* 条数 */
    
#else /* EG_PLATFORM_LINUX */
    /* 文件存储配置 */
    #define EG_DATA_DIR             "/var/lib/edge-gateway"
    #define EG_LOG_DIR              "/var/log/edge-gateway"
    #define EG_CONFIG_DIR           "/etc/edge-gateway"
#endif

/*============================================================================
 *                              调试配置
 *===========================================================================*/

/* 编译时开关 */
#if defined(DEBUG) || defined(_DEBUG)
    #define EG_DEBUG                1
    #define EG_ASSERT_ENABLE        1
    #define EG_TRACE_ENABLE         1
#else
    #define EG_DEBUG                0
    #define EG_ASSERT_ENABLE        0
    #define EG_TRACE_ENABLE         0
#endif

/* 性能统计 */
#define EG_PERF_STATS_ENABLE        1

/* 内存检测 */
#if defined(EG_PLATFORM_LINUX) && EG_DEBUG
    #define EG_MEM_DEBUG            1
    #define EG_MEM_GUARD_ENABLE     1
#endif

/*============================================================================
 *                              断言宏
 *===========================================================================*/

#if EG_ASSERT_ENABLE
    #define EG_ASSERT(expr) do { \
        if (EG_UNLIKELY(!(expr))) { \
            eg_assert_fail(__FILE__, __LINE__, __func__, #expr); \
        } \
    } while(0)
    
    void eg_assert_fail(const char* file, int line, const char* func, const char* expr);
#else
    #define EG_ASSERT(expr)     ((void)0)
#endif

/*============================================================================
 *                              特性开关
 *===========================================================================*/

/* 协议支持 */
#define EG_FEATURE_MODBUS           1
#define EG_FEATURE_MQTT             1
#define EG_FEATURE_CUSTOM_PROTO     1

/* 加密支持 */
#define EG_FEATURE_AES              1
#define EG_FEATURE_TLS              EG_PLATFORM_LINUX

/* 压缩支持 */
#define EG_FEATURE_ZLIB             EG_PLATFORM_LINUX
#define EG_FEATURE_LZ4              1

/* OTA支持 */
#define EG_FEATURE_OTA              1

#ifdef __cplusplus
}
#endif

#endif /* EDGE_GATEWAY_CONFIG_H */
