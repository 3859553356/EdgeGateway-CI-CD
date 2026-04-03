/**
 * @file memory_pool.c
 * @brief 高性能内存池实现
 * @version 1.0.0
 */

#include "memory_pool.h"
#include <string.h>

#if defined(EG_PLATFORM_LINUX)
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#endif

#if defined(EG_PLATFORM_EMBEDDED)
#include "FreeRTOS.h"
#include "semphr.h"
#endif

/*============================================================================
 *                              静态变量
 *===========================================================================*/

/* 默认内存池 */
static eg_mempool_t s_default_pool;
static bool s_default_pool_initialized = false;

#if defined(EG_PLATFORM_EMBEDDED)
/* 嵌入式静态内存 */
static u8 s_pool_buffer[EG_MEM_POOL_SIZE] EG_ALIGNED(8) EG_SECTION(".pool");
#endif

/* 大小类别表 */
static const u32 s_size_classes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
static const u32 s_block_counts[] = {512, 256, 128, 64, 32, 16, 8, 4};

/*============================================================================
 *                              平台抽象
 *===========================================================================*/

#if defined(EG_PLATFORM_LINUX)
typedef pthread_spinlock_t mempool_lock_t;
#define LOCK_INIT(lock)     pthread_spin_init((pthread_spinlock_t*)(lock), PTHREAD_PROCESS_PRIVATE)
#define LOCK_DESTROY(lock)  pthread_spin_destroy((pthread_spinlock_t*)(lock))
#define LOCK_ACQUIRE(lock)  pthread_spin_lock((pthread_spinlock_t*)(lock))
#define LOCK_RELEASE(lock)  pthread_spin_unlock((pthread_spinlock_t*)(lock))
#elif defined(EG_PLATFORM_EMBEDDED)
typedef SemaphoreHandle_t mempool_lock_t;
#define LOCK_INIT(lock)     (*(SemaphoreHandle_t*)(lock) = xSemaphoreCreateMutex())
#define LOCK_DESTROY(lock)  vSemaphoreDelete(*(SemaphoreHandle_t*)(lock))
#define LOCK_ACQUIRE(lock)  xSemaphoreTake(*(SemaphoreHandle_t*)(lock), portMAX_DELAY)
#define LOCK_RELEASE(lock)  xSemaphoreGive(*(SemaphoreHandle_t*)(lock))
#endif

/*============================================================================
 *                              辅助函数
 *===========================================================================*/

/**
 * @brief 查找适合的大小类别
 */
static int find_size_class(eg_mempool_t* pool, usize size) {
    for (u32 i = 0; i < pool->num_classes; i++) {
        if (size <= pool->size_classes[i].user_size) {
            return (int)i;
        }
    }
    return -1;  /* 需要使用伙伴算法 */
}

/**
 * @brief 伙伴算法 - 计算阶数
 */
static u32 buddy_order(usize size) {
    u32 order = 0;
    size = (size + sizeof(eg_mem_block_t) + 31) & ~31;  /* 32字节对齐 */
    while ((1UL << order) < size) {
        order++;
    }
    return order;
}

/**
 * @brief 初始化大小类别
 */
static eg_error_t init_size_classes(eg_mempool_t* pool, u8* buffer, usize* offset) {
    pool->num_classes = EG_MEMPOOL_SIZE_CLASS_COUNT;
    
    for (u32 i = 0; i < pool->num_classes; i++) {
        eg_size_class_t* sc = &pool->size_classes[i];
        
        sc->block_size = s_size_classes[i];
        sc->user_size = sc->block_size - sizeof(eg_mem_block_t);
        sc->total_count = s_block_counts[i];
        sc->free_count = sc->total_count;
        sc->free_list = NULL;
        sc->alloc_count = 0;
        sc->free_count_stat = 0;
        sc->peak_usage = 0;
        
        /* 分配内存块 */
        for (u32 j = 0; j < sc->total_count; j++) {
            if (*offset + sc->block_size > pool->pool_size) {
                return EG_ERR_NOMEM;
            }
            
            eg_mem_block_t* block = (eg_mem_block_t*)(buffer + *offset);
            block->size_class = (u16)i;
            block->flags = 0;
            block->next = sc->free_list;
            sc->free_list = block;
            
            *offset += sc->block_size;
        }
    }
    
    return EG_OK;
}

/*============================================================================
 *                              全局内存池
 *===========================================================================*/

eg_mempool_t* eg_mempool_default(void) {
    return &s_default_pool;
}

eg_error_t eg_mempool_init_default(void) {
    if (s_default_pool_initialized) {
        return EG_OK;
    }
    
#if defined(EG_PLATFORM_EMBEDDED)
    eg_error_t err = eg_mempool_create(&s_default_pool, s_pool_buffer, 
                                        sizeof(s_pool_buffer), EG_MEM_ALIGNMENT);
#else
    eg_error_t err = eg_mempool_create(&s_default_pool, NULL, 
                                        EG_MEM_POOL_SIZE, EG_MEM_ALIGNMENT);
#endif
    
    if (err == EG_OK) {
        s_default_pool_initialized = true;
    }
    return err;
}

void eg_mempool_deinit_default(void) {
    if (s_default_pool_initialized) {
        eg_mempool_destroy(&s_default_pool);
        s_default_pool_initialized = false;
    }
}

/*============================================================================
 *                              内存池API
 *===========================================================================*/

eg_error_t eg_mempool_create(eg_mempool_t* pool, void* buffer, usize size, u32 alignment) {
    if (!pool || size == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    memset(pool, 0, sizeof(eg_mempool_t));
    pool->alignment = alignment ? alignment : EG_MEM_ALIGNMENT;
    pool->pool_size = size;
    
#if defined(EG_PLATFORM_LINUX)
    if (!buffer) {
        /* Linux: mmap分配 */
        buffer = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (buffer == MAP_FAILED) {
            return EG_ERR_NOMEM;
        }
    }
    
    /* 分配锁 */
    pool->lock = malloc(sizeof(pthread_spinlock_t));
    if (!pool->lock) {
        munmap(buffer, size);
        return EG_ERR_NOMEM;
    }
    LOCK_INIT(pool->lock);
#elif defined(EG_PLATFORM_EMBEDDED)
    if (!buffer) {
        return EG_ERR_INVALID_PARAM;  /* 嵌入式必须传入静态缓冲区 */
    }
    pool->mutex = pvPortMalloc(sizeof(SemaphoreHandle_t));
    if (!pool->mutex) {
        return EG_ERR_NOMEM;
    }
    LOCK_INIT(pool->mutex);
#endif
    
    pool->pool_start = (u8*)buffer;
    
    /* 初始化大小类别 */
    usize offset = 0;
    eg_error_t err = init_size_classes(pool, pool->pool_start, &offset);
    if (err != EG_OK) {
        eg_mempool_destroy(pool);
        return err;
    }
    
    /* 剩余空间用于伙伴分配 */
    pool->buddy_start = pool->pool_start + offset;
    pool->buddy_size = size - offset;
    
    /* 计算伙伴算法最大阶数 */
    pool->buddy_order_max = 0;
    usize buddy_block = 32;  /* 最小32字节 */
    while (buddy_block < pool->buddy_size) {
        pool->buddy_order_max++;
        buddy_block <<= 1;
    }
    
    pool->initialized = true;
    return EG_OK;
}

void eg_mempool_destroy(eg_mempool_t* pool) {
    if (!pool || !pool->initialized) {
        return;
    }
    
#if defined(EG_PLATFORM_LINUX)
    if (pool->lock) {
        LOCK_DESTROY(pool->lock);
        free(pool->lock);
    }
    if (pool->pool_start) {
        munmap(pool->pool_start, pool->pool_size);
    }
#elif defined(EG_PLATFORM_EMBEDDED)
    if (pool->mutex) {
        LOCK_DESTROY(pool->mutex);
        vPortFree(pool->mutex);
    }
#endif
    
    memset(pool, 0, sizeof(eg_mempool_t));
}

void* eg_mempool_alloc(eg_mempool_t* pool, usize size) {
    if (!pool || !pool->initialized || size == 0) {
        return NULL;
    }
    
    void* ptr = NULL;
    
#if defined(EG_PLATFORM_LINUX)
    LOCK_ACQUIRE(pool->lock);
#elif defined(EG_PLATFORM_EMBEDDED)
    LOCK_ACQUIRE(pool->mutex);
#endif
    
    /* 查找合适的大小类别 */
    int class_idx = find_size_class(pool, size);
    
    if (class_idx >= 0) {
        eg_size_class_t* sc = &pool->size_classes[class_idx];
        
        if (sc->free_list) {
            eg_mem_block_t* block = sc->free_list;
            sc->free_list = block->next;
            sc->free_count--;
            sc->alloc_count++;
            
            /* 更新峰值 */
            u32 used = sc->total_count - sc->free_count;
            if (used > sc->peak_usage) {
                sc->peak_usage = used;
            }
            
            ptr = (u8*)block + sizeof(eg_mem_block_t);
            
#if EG_MEM_DEBUG
            block->guard_head = EG_MEMPOOL_GUARD_HEAD;
            memset(ptr, EG_MEMPOOL_ALLOC_PATTERN, sc->user_size);
#endif
        }
    } else {
        /* 大块分配 - 使用伙伴算法 */
        /* 简化实现: 直接从伙伴区分配 */
        usize alloc_size = EG_ALIGN_UP(size + sizeof(eg_mem_block_t), 32);
        if (pool->current_usage + alloc_size <= pool->buddy_size) {
            eg_mem_block_t* block = (eg_mem_block_t*)(pool->buddy_start + pool->current_usage);
            block->size_class = 0xFF;  /* 标记为大块 */
            block->flags = (u16)(alloc_size >> 16);
            block->next = (eg_mem_block_t*)(uptr)alloc_size;  /* 存储大小 */
            
            pool->current_usage += alloc_size;
            ptr = (u8*)block + sizeof(eg_mem_block_t);
        }
    }
    
    if (ptr) {
        pool->total_alloc++;
        pool->current_usage += size;
        if (pool->current_usage > pool->peak_usage) {
            pool->peak_usage = pool->current_usage;
        }
    } else {
        pool->fail_count++;
    }
    
#if defined(EG_PLATFORM_LINUX)
    LOCK_RELEASE(pool->lock);
#elif defined(EG_PLATFORM_EMBEDDED)
    LOCK_RELEASE(pool->mutex);
#endif
    
    return ptr;
}

void* eg_mempool_calloc(eg_mempool_t* pool, usize count, usize size) {
    usize total = count * size;
    void* ptr = eg_mempool_alloc(pool, total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* eg_mempool_realloc(eg_mempool_t* pool, void* ptr, usize new_size) {
    if (!ptr) {
        return eg_mempool_alloc(pool, new_size);
    }
    if (new_size == 0) {
        eg_mempool_free(pool, ptr);
        return NULL;
    }
    
    /* 获取原块大小 */
    eg_mem_block_t* block = (eg_mem_block_t*)((u8*)ptr - sizeof(eg_mem_block_t));
    usize old_size;
    
    if (block->size_class < pool->num_classes) {
        old_size = pool->size_classes[block->size_class].user_size;
    } else {
        old_size = (usize)(uptr)block->next - sizeof(eg_mem_block_t);
    }
    
    /* 如果新大小适合当前块，直接返回 */
    if (new_size <= old_size) {
        return ptr;
    }
    
    /* 分配新块并复制 */
    void* new_ptr = eg_mempool_alloc(pool, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        eg_mempool_free(pool, ptr);
    }
    return new_ptr;
}

void* eg_mempool_aligned_alloc(eg_mempool_t* pool, usize size, usize alignment) {
    /* 分配额外空间用于对齐 */
    usize total_size = size + alignment - 1 + sizeof(void*);
    void* raw = eg_mempool_alloc(pool, total_size);
    if (!raw) {
        return NULL;
    }
    
    /* 计算对齐地址 */
    uptr aligned = EG_ALIGN_UP((uptr)raw + sizeof(void*), alignment);
    
    /* 在对齐地址前保存原始指针 */
    ((void**)aligned)[-1] = raw;
    
    return (void*)aligned;
}

void eg_mempool_free(eg_mempool_t* pool, void* ptr) {
    if (!pool || !pool->initialized || !ptr) {
        return;
    }
    
#if defined(EG_PLATFORM_LINUX)
    LOCK_ACQUIRE(pool->lock);
#elif defined(EG_PLATFORM_EMBEDDED)
    LOCK_ACQUIRE(pool->mutex);
#endif
    
    eg_mem_block_t* block = (eg_mem_block_t*)((u8*)ptr - sizeof(eg_mem_block_t));
    
#if EG_MEM_DEBUG
    /* 检查守卫 */
    if (block->guard_head != EG_MEMPOOL_GUARD_HEAD) {
        /* 内存损坏! */
#if defined(EG_PLATFORM_LINUX)
        LOCK_RELEASE(pool->lock);
#elif defined(EG_PLATFORM_EMBEDDED)
        LOCK_RELEASE(pool->mutex);
#endif
        return;
    }
#endif
    
    if (block->size_class < pool->num_classes) {
        eg_size_class_t* sc = &pool->size_classes[block->size_class];
        
#if EG_MEM_DEBUG
        memset(ptr, EG_MEMPOOL_FREE_PATTERN, sc->user_size);
#endif
        
        block->next = sc->free_list;
        sc->free_list = block;
        sc->free_count++;
        sc->free_count_stat++;
        
        pool->current_usage -= sc->user_size;
    }
    /* 大块暂不回收(简化实现) */
    
    pool->total_free++;
    
#if defined(EG_PLATFORM_LINUX)
    LOCK_RELEASE(pool->lock);
#elif defined(EG_PLATFORM_EMBEDDED)
    LOCK_RELEASE(pool->mutex);
#endif
}

eg_error_t eg_mempool_get_stats(eg_mempool_t* pool, eg_mempool_stats_t* stats) {
    if (!pool || !stats) {
        return EG_ERR_INVALID_PARAM;
    }
    
    memset(stats, 0, sizeof(eg_mempool_stats_t));
    stats->pool_size = pool->pool_size;
    
    usize total_free = 0;
    for (u32 i = 0; i < pool->num_classes; i++) {
        total_free += pool->size_classes[i].free_count * pool->size_classes[i].user_size;
    }
    
    stats->free_size = total_free;
    stats->used_size = pool->pool_size - total_free;
    stats->utilization = (f32)stats->used_size / (f32)stats->pool_size;
    stats->fragmentation = 0.0f;  /* 需要更复杂的计算 */
    stats->alloc_count = pool->total_alloc;
    stats->free_count = pool->total_free;
    stats->fail_count = pool->fail_count;
    stats->peak_usage = pool->peak_usage;
    
    return EG_OK;
}

eg_error_t eg_mempool_check(eg_mempool_t* pool) {
    if (!pool || !pool->initialized) {
        return EG_ERR_INVALID_PARAM;
    }
    
    /* 检查每个大小类别的空闲链表 */
    for (u32 i = 0; i < pool->num_classes; i++) {
        eg_size_class_t* sc = &pool->size_classes[i];
        u32 count = 0;
        eg_mem_block_t* block = sc->free_list;
        
        while (block) {
            count++;
            if (count > sc->total_count) {
                return EG_ERR_OVERFLOW;  /* 链表损坏 */
            }
            block = block->next;
        }
        
        if (count != sc->free_count) {
            return EG_ERR_GENERIC;  /* 计数不匹配 */
        }
    }
    
    return EG_OK;
}

void eg_mempool_dump(eg_mempool_t* pool) {
    if (!pool) return;
    
    /* 打印统计信息 - 使用平台相关的输出 */
#if defined(EG_PLATFORM_LINUX)
    #include <stdio.h>
    printf("=== Memory Pool Stats ===\n");
    printf("Total Size: %zu bytes\n", pool->pool_size);
    printf("Total Alloc: %llu\n", (unsigned long long)pool->total_alloc);
    printf("Total Free: %llu\n", (unsigned long long)pool->total_free);
    printf("Fail Count: %llu\n", (unsigned long long)pool->fail_count);
    printf("\nSize Classes:\n");
    for (u32 i = 0; i < pool->num_classes; i++) {
        eg_size_class_t* sc = &pool->size_classes[i];
        printf("  [%u] %u bytes: %u/%u free (peak: %llu)\n",
               i, sc->user_size, sc->free_count, sc->total_count,
               (unsigned long long)sc->peak_usage);
    }
#endif
}

/*============================================================================
 *                              对象池
 *===========================================================================*/

eg_error_t eg_objpool_create(eg_object_pool_t* pool, eg_mempool_t* mempool,
                              usize object_size, usize capacity) {
    if (!pool || object_size == 0 || capacity == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    memset(pool, 0, sizeof(eg_object_pool_t));
    pool->mempool = mempool ? mempool : eg_mempool_default();
    pool->object_size = object_size;
    pool->capacity = capacity;
    
    /* 分配空闲栈 */
    pool->free_stack = (void**)eg_mempool_alloc(pool->mempool, capacity * sizeof(void*));
    if (!pool->free_stack) {
        return EG_ERR_NOMEM;
    }
    
    /* 预分配所有对象 */
    for (usize i = 0; i < capacity; i++) {
        void* obj = eg_mempool_alloc(pool->mempool, object_size);
        if (!obj) {
            /* 回滚 */
            while (i > 0) {
                eg_mempool_free(pool->mempool, pool->free_stack[--i]);
            }
            eg_mempool_free(pool->mempool, pool->free_stack);
            return EG_ERR_NOMEM;
        }
        pool->free_stack[i] = obj;
    }
    
    pool->free_top = (u32)capacity;
    return EG_OK;
}

void eg_objpool_destroy(eg_object_pool_t* pool) {
    if (!pool || !pool->free_stack) {
        return;
    }
    
    /* 释放所有对象 */
    for (u32 i = 0; i < pool->free_top; i++) {
        if (pool->destruct) {
            pool->destruct(pool->free_stack[i]);
        }
        eg_mempool_free(pool->mempool, pool->free_stack[i]);
    }
    
    eg_mempool_free(pool->mempool, pool->free_stack);
    memset(pool, 0, sizeof(eg_object_pool_t));
}

void* eg_objpool_acquire(eg_object_pool_t* pool) {
    if (!pool || pool->free_top == 0) {
        return NULL;
    }
    
    void* obj = pool->free_stack[--pool->free_top];
    pool->alloc_count++;
    
    if (pool->construct) {
        if (pool->construct(obj, pool->construct_arg) != EG_OK) {
            pool->free_stack[pool->free_top++] = obj;
            pool->alloc_count--;
            return NULL;
        }
    }
    
    return obj;
}

void eg_objpool_release(eg_object_pool_t* pool, void* obj) {
    if (!pool || !obj || pool->free_top >= pool->capacity) {
        return;
    }
    
    if (pool->destruct) {
        pool->destruct(obj);
    }
    
    pool->free_stack[pool->free_top++] = obj;
    pool->alloc_count--;
}

void eg_objpool_set_callbacks(eg_object_pool_t* pool,
                               eg_error_t (*construct)(void*, void*),
                               void (*destruct)(void*),
                               void* arg) {
    if (pool) {
        pool->construct = construct;
        pool->destruct = destruct;
        pool->construct_arg = arg;
    }
}

/*============================================================================
 *                              调试版本
 *===========================================================================*/

#if EG_MEM_DEBUG
void* eg_mempool_alloc_debug(eg_mempool_t* pool, usize size, const char* file, int line) {
    void* ptr = eg_mempool_alloc(pool, size);
    if (ptr) {
        eg_mem_block_t* block = (eg_mem_block_t*)((u8*)ptr - sizeof(eg_mem_block_t));
        block->alloc_file = file;
        block->alloc_line = line;
        /* block->alloc_time = get_timestamp(); */
    }
    return ptr;
}

void* eg_mempool_calloc_debug(eg_mempool_t* pool, usize n, usize size, const char* file, int line) {
    void* ptr = eg_mempool_alloc_debug(pool, n * size, file, line);
    if (ptr) {
        memset(ptr, 0, n * size);
    }
    return ptr;
}

void* eg_mempool_realloc_debug(eg_mempool_t* pool, void* ptr, usize size, const char* file, int line) {
    (void)file;
    (void)line;
    return eg_mempool_realloc(pool, ptr, size);
}

void eg_mempool_free_debug(eg_mempool_t* pool, void* ptr, const char* file, int line) {
    (void)file;
    (void)line;
    eg_mempool_free(pool, ptr);
}
#endif
