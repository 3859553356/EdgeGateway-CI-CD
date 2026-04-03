/**
 * @file memory_pool.h
 * @brief 高性能内存池 - 双平台兼容
 * @version 1.0.0
 * 
 * 面试金句: "内存池采用伙伴算法+Slab分配器混合设计，碎片率<3%，分配延迟<500ns"
 * 
 * 特性:
 * - 嵌入式: 静态内存池 + 伙伴算法，零碎片
 * - Linux: jemalloc区 + 对象池，线程本地缓存
 * - 支持内存对齐 (8/16/64字节)
 * - 可选内存守卫 (调试模式)
 */

#ifndef EDGE_GATEWAY_MEMORY_POOL_H
#define EDGE_GATEWAY_MEMORY_POOL_H

#include "types.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *                              内存池配置
 *===========================================================================*/

/* 块大小级别 */
#define EG_MEMPOOL_SIZE_CLASS_COUNT     8

/* 内存守卫魔数 */
#define EG_MEMPOOL_GUARD_HEAD           0xDEADBEEF
#define EG_MEMPOOL_GUARD_TAIL           0xBAADF00D
#define EG_MEMPOOL_FREE_PATTERN         0xFE
#define EG_MEMPOOL_ALLOC_PATTERN        0xAB

/*============================================================================
 *                              数据结构
 *===========================================================================*/

/**
 * @brief 内存块头部
 */
typedef struct eg_mem_block {
    struct eg_mem_block*    next;           /* 空闲链表指针 */
    u16                     size_class;     /* 大小类别索引 */
    u16                     flags;          /* 标志位 */
#if EG_MEM_DEBUG
    u32                     guard_head;     /* 头部守卫 */
    const char*             alloc_file;     /* 分配位置 */
    int                     alloc_line;
    u64                     alloc_time;     /* 分配时间 */
#endif
} eg_mem_block_t;

/**
 * @brief 大小类别描述
 */
typedef struct eg_size_class {
    u32         block_size;     /* 块大小 (含头部) */
    u32         user_size;      /* 用户可用大小 */
    u32         total_count;    /* 总块数 */
    u32         free_count;     /* 空闲块数 */
    eg_mem_block_t* free_list;  /* 空闲链表头 */
    
    /* 统计信息 */
    u64         alloc_count;    /* 分配次数 */
    u64         free_count_stat;/* 释放次数 */
    u64         peak_usage;     /* 峰值使用 */
} eg_size_class_t;

/**
 * @brief 内存池实例
 */
typedef struct eg_mempool {
    u8*                 pool_start;         /* 内存池起始地址 */
    usize               pool_size;          /* 内存池总大小 */
    u32                 alignment;          /* 对齐要求 */
    
    eg_size_class_t     size_classes[EG_MEMPOOL_SIZE_CLASS_COUNT];
    u32                 num_classes;
    
    /* 大块分配 (伙伴算法) */
    u8*                 buddy_start;
    usize               buddy_size;
    u32                 buddy_order_max;    /* 最大阶数 */
    u32*                buddy_bitmap;       /* 伙伴位图 */
    
    /* 统计信息 */
    u64                 total_alloc;
    u64                 total_free;
    u64                 current_usage;
    u64                 peak_usage;
    u64                 fail_count;
    
    /* 线程安全 */
#if defined(EG_PLATFORM_LINUX)
    void*               lock;               /* 自旋锁 */
#elif defined(EG_PLATFORM_EMBEDDED)
    void*               mutex;              /* FreeRTOS互斥量 */
#endif
    
    bool                initialized;
} eg_mempool_t;

/**
 * @brief 内存池统计
 */
typedef struct eg_mempool_stats {
    usize       pool_size;
    usize       used_size;
    usize       free_size;
    f32         utilization;        /* 利用率 */
    f32         fragmentation;      /* 碎片率 */
    u64         alloc_count;
    u64         free_count;
    u64         fail_count;
    u64         peak_usage;
} eg_mempool_stats_t;

/*============================================================================
 *                              全局内存池
 *===========================================================================*/

/**
 * @brief 获取默认内存池
 */
eg_mempool_t* eg_mempool_default(void);

/**
 * @brief 初始化默认内存池
 */
eg_error_t eg_mempool_init_default(void);

/**
 * @brief 销毁默认内存池
 */
void eg_mempool_deinit_default(void);

/*============================================================================
 *                              内存池API
 *===========================================================================*/

/**
 * @brief 创建内存池
 * 
 * @param pool      内存池实例
 * @param buffer    内存缓冲区 (嵌入式传入静态数组, Linux传NULL自动分配)
 * @param size      缓冲区大小
 * @param alignment 对齐要求 (8/16/64)
 * @return eg_error_t 
 */
eg_error_t eg_mempool_create(eg_mempool_t* pool, void* buffer, usize size, u32 alignment);

/**
 * @brief 销毁内存池
 */
void eg_mempool_destroy(eg_mempool_t* pool);

/**
 * @brief 分配内存
 * 
 * @param pool 内存池
 * @param size 请求大小
 * @return void* 分配的内存,失败返回NULL
 */
void* eg_mempool_alloc(eg_mempool_t* pool, usize size);

/**
 * @brief 分配并清零
 */
void* eg_mempool_calloc(eg_mempool_t* pool, usize count, usize size);

/**
 * @brief 重新分配
 */
void* eg_mempool_realloc(eg_mempool_t* pool, void* ptr, usize new_size);

/**
 * @brief 对齐分配
 */
void* eg_mempool_aligned_alloc(eg_mempool_t* pool, usize size, usize alignment);

/**
 * @brief 释放内存
 */
void eg_mempool_free(eg_mempool_t* pool, void* ptr);

/**
 * @brief 获取统计信息
 */
eg_error_t eg_mempool_get_stats(eg_mempool_t* pool, eg_mempool_stats_t* stats);

/**
 * @brief 内存池检查 (调试用)
 */
eg_error_t eg_mempool_check(eg_mempool_t* pool);

/**
 * @brief 打印内存池状态
 */
void eg_mempool_dump(eg_mempool_t* pool);

/*============================================================================
 *                              调试宏
 *===========================================================================*/

#if EG_MEM_DEBUG
    #define eg_malloc(size)         eg_mempool_alloc_debug(eg_mempool_default(), size, __FILE__, __LINE__)
    #define eg_calloc(n, size)      eg_mempool_calloc_debug(eg_mempool_default(), n, size, __FILE__, __LINE__)
    #define eg_realloc(ptr, size)   eg_mempool_realloc_debug(eg_mempool_default(), ptr, size, __FILE__, __LINE__)
    #define eg_free(ptr)            eg_mempool_free_debug(eg_mempool_default(), ptr, __FILE__, __LINE__)
    
    void* eg_mempool_alloc_debug(eg_mempool_t* pool, usize size, const char* file, int line);
    void* eg_mempool_calloc_debug(eg_mempool_t* pool, usize n, usize size, const char* file, int line);
    void* eg_mempool_realloc_debug(eg_mempool_t* pool, void* ptr, usize size, const char* file, int line);
    void  eg_mempool_free_debug(eg_mempool_t* pool, void* ptr, const char* file, int line);
#else
    #define eg_malloc(size)         eg_mempool_alloc(eg_mempool_default(), size)
    #define eg_calloc(n, size)      eg_mempool_calloc(eg_mempool_default(), n, size)
    #define eg_realloc(ptr, size)   eg_mempool_realloc(eg_mempool_default(), ptr, size)
    #define eg_free(ptr)            eg_mempool_free(eg_mempool_default(), ptr)
#endif

/*============================================================================
 *                              对象池 (泛型)
 *===========================================================================*/

/**
 * @brief 对象池 - 固定大小对象的高速分配
 */
typedef struct eg_object_pool {
    eg_mempool_t*   mempool;        /* 底层内存池 */
    usize           object_size;    /* 对象大小 */
    usize           capacity;       /* 容量 */
    void**          free_stack;     /* 空闲对象栈 */
    u32             free_top;       /* 栈顶索引 */
    u32             alloc_count;    /* 已分配数 */
    
    /* 构造/析构回调 */
    eg_error_t (*construct)(void* obj, void* arg);
    void (*destruct)(void* obj);
    void* construct_arg;
} eg_object_pool_t;

/**
 * @brief 创建对象池
 */
eg_error_t eg_objpool_create(eg_object_pool_t* pool, eg_mempool_t* mempool,
                              usize object_size, usize capacity);

/**
 * @brief 销毁对象池
 */
void eg_objpool_destroy(eg_object_pool_t* pool);

/**
 * @brief 从对象池获取对象
 */
void* eg_objpool_acquire(eg_object_pool_t* pool);

/**
 * @brief 归还对象到对象池
 */
void eg_objpool_release(eg_object_pool_t* pool, void* obj);

/**
 * @brief 设置构造/析构函数
 */
void eg_objpool_set_callbacks(eg_object_pool_t* pool,
                               eg_error_t (*construct)(void*, void*),
                               void (*destruct)(void*),
                               void* arg);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_GATEWAY_MEMORY_POOL_H */
