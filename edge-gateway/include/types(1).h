/**
 * @file types.h
 * @brief 通用类型定义 - 双平台兼容
 * @author EdgeGateway Team
 * @version 1.0.0
 * @date 2024
 * 
 * 面试金句: "统一类型系统实现双平台零修改编译，大小端自动适配覆盖ARM/x86"
 */

#ifndef EDGE_GATEWAY_TYPES_H
#define EDGE_GATEWAY_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *                              平台检测
 *===========================================================================*/

/* 编译器检测 */
#if defined(__GNUC__)
    #define EG_COMPILER_GCC     1
    #define EG_GCC_VERSION      (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#elif defined(__clang__)
    #define EG_COMPILER_CLANG   1
#elif defined(_MSC_VER)
    #define EG_COMPILER_MSVC    1
#endif

/* 平台检测 */
#if defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH)
    #define EG_PLATFORM_ARM     1
    #define EG_PLATFORM_EMBEDDED 1
#elif defined(__x86_64__) || defined(_M_X64)
    #define EG_PLATFORM_X64     1
    #define EG_PLATFORM_LINUX   1
#elif defined(__i386__) || defined(_M_IX86)
    #define EG_PLATFORM_X86     1
#endif

/* FreeRTOS检测 */
#if defined(FREERTOS) || defined(configUSE_PREEMPTION)
    #define EG_RTOS_FREERTOS    1
#endif

/*============================================================================
 *                              基础类型
 *===========================================================================*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

/* 固定宽度整数类型别名 */
typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;

/* 指针大小类型 */
typedef uintptr_t   uptr;
typedef intptr_t    sptr;
typedef size_t      usize;
typedef ptrdiff_t   ssize;

/* 浮点类型 */
typedef float       f32;
typedef double      f64;

/*============================================================================
 *                              字节序处理
 *===========================================================================*/

/* 字节序检测 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define EG_BIG_ENDIAN       1
#else
    #define EG_LITTLE_ENDIAN    1
#endif

/* 字节交换宏 */
#define EG_SWAP16(x) ((u16)(((x) >> 8) | ((x) << 8)))
#define EG_SWAP32(x) ((u32)(((x) >> 24) | (((x) >> 8) & 0xFF00) | \
                            (((x) << 8) & 0xFF0000) | ((x) << 24)))
#define EG_SWAP64(x) ((u64)(((x) >> 56) | (((x) >> 40) & 0xFF00ULL) | \
                            (((x) >> 24) & 0xFF0000ULL) | (((x) >> 8) & 0xFF000000ULL) | \
                            (((x) << 8) & 0xFF00000000ULL) | (((x) << 24) & 0xFF0000000000ULL) | \
                            (((x) << 40) & 0xFF000000000000ULL) | ((x) << 56)))

/* 网络字节序转换 (大端) */
#if defined(EG_LITTLE_ENDIAN)
    #define eg_hton16(x)    EG_SWAP16(x)
    #define eg_hton32(x)    EG_SWAP32(x)
    #define eg_hton64(x)    EG_SWAP64(x)
    #define eg_ntoh16(x)    EG_SWAP16(x)
    #define eg_ntoh32(x)    EG_SWAP32(x)
    #define eg_ntoh64(x)    EG_SWAP64(x)
#else
    #define eg_hton16(x)    (x)
    #define eg_hton32(x)    (x)
    #define eg_hton64(x)    (x)
    #define eg_ntoh16(x)    (x)
    #define eg_ntoh32(x)    (x)
    #define eg_ntoh64(x)    (x)
#endif

/*============================================================================
 *                              编译器属性
 *===========================================================================*/

#if defined(EG_COMPILER_GCC) || defined(EG_COMPILER_CLANG)
    #define EG_PACKED           __attribute__((packed))
    #define EG_ALIGNED(n)       __attribute__((aligned(n)))
    #define EG_UNUSED           __attribute__((unused))
    #define EG_LIKELY(x)        __builtin_expect(!!(x), 1)
    #define EG_UNLIKELY(x)      __builtin_expect(!!(x), 0)
    #define EG_INLINE           static inline __attribute__((always_inline))
    #define EG_NOINLINE         __attribute__((noinline))
    #define EG_NORETURN         __attribute__((noreturn))
    #define EG_WEAK             __attribute__((weak))
    #define EG_SECTION(s)       __attribute__((section(s)))
    #define EG_DEPRECATED       __attribute__((deprecated))
    #define EG_PURE             __attribute__((pure))
    #define EG_CONST            __attribute__((const))
    #define EG_FORMAT(a,b,c)    __attribute__((format(a, b, c)))
#else
    #define EG_PACKED
    #define EG_ALIGNED(n)
    #define EG_UNUSED
    #define EG_LIKELY(x)        (x)
    #define EG_UNLIKELY(x)      (x)
    #define EG_INLINE           static inline
    #define EG_NOINLINE
    #define EG_NORETURN
    #define EG_WEAK
    #define EG_SECTION(s)
    #define EG_DEPRECATED
    #define EG_PURE
    #define EG_CONST
    #define EG_FORMAT(a,b,c)
#endif

/*============================================================================
 *                              内存屏障
 *===========================================================================*/

#if defined(EG_COMPILER_GCC) || defined(EG_COMPILER_CLANG)
    #define eg_memory_barrier()         __sync_synchronize()
    #define eg_read_barrier()           __asm__ __volatile__("" ::: "memory")
    #define eg_write_barrier()          __asm__ __volatile__("" ::: "memory")
    #define eg_compiler_barrier()       __asm__ __volatile__("" ::: "memory")
#else
    #define eg_memory_barrier()
    #define eg_read_barrier()
    #define eg_write_barrier()
    #define eg_compiler_barrier()
#endif

/*============================================================================
 *                              原子操作
 *===========================================================================*/

#if defined(EG_COMPILER_GCC) || defined(EG_COMPILER_CLANG)
    #define eg_atomic_load(ptr)             __atomic_load_n(ptr, __ATOMIC_SEQ_CST)
    #define eg_atomic_store(ptr, val)       __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST)
    #define eg_atomic_add(ptr, val)         __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST)
    #define eg_atomic_sub(ptr, val)         __atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST)
    #define eg_atomic_cas(ptr, exp, des)    __atomic_compare_exchange_n(ptr, exp, des, 0, \
                                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#endif

/*============================================================================
 *                              通用宏
 *===========================================================================*/

/* 数组大小 */
#define EG_ARRAY_SIZE(arr)      (sizeof(arr) / sizeof((arr)[0]))

/* 位操作 */
#define EG_BIT(n)               (1UL << (n))
#define EG_BIT_SET(x, n)        ((x) |= EG_BIT(n))
#define EG_BIT_CLR(x, n)        ((x) &= ~EG_BIT(n))
#define EG_BIT_TST(x, n)        (((x) & EG_BIT(n)) != 0)
#define EG_BIT_FLIP(x, n)       ((x) ^= EG_BIT(n))

/* 对齐操作 */
#define EG_ALIGN_UP(x, align)   (((x) + ((align) - 1)) & ~((align) - 1))
#define EG_ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define EG_IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)
#define EG_IS_POWER_OF_2(x)     (((x) != 0) && (((x) & ((x) - 1)) == 0))

/* 最大最小 */
#define EG_MIN(a, b)            (((a) < (b)) ? (a) : (b))
#define EG_MAX(a, b)            (((a) > (b)) ? (a) : (b))
#define EG_CLAMP(x, lo, hi)     EG_MIN(EG_MAX(x, lo), hi)

/* 指针操作 */
#define EG_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* 字符串化 */
#define EG_STRINGIFY(x)         #x
#define EG_TOSTRING(x)          EG_STRINGIFY(x)

/* 连接 */
#define EG_CONCAT(a, b)         a##b
#define EG_CONCAT3(a, b, c)     a##b##c

/*============================================================================
 *                              结果类型
 *===========================================================================*/

/**
 * @brief 错误码定义
 */
typedef enum eg_error {
    EG_OK                   = 0,        /* 成功 */
    EG_ERR_GENERIC          = -1,       /* 通用错误 */
    EG_ERR_NOMEM            = -2,       /* 内存不足 */
    EG_ERR_INVALID_PARAM    = -3,       /* 无效参数 */
    EG_ERR_TIMEOUT          = -4,       /* 超时 */
    EG_ERR_BUSY             = -5,       /* 忙碌 */
    EG_ERR_NOT_FOUND        = -6,       /* 未找到 */
    EG_ERR_ALREADY_EXISTS   = -7,       /* 已存在 */
    EG_ERR_NOT_SUPPORTED    = -8,       /* 不支持 */
    EG_ERR_IO               = -9,       /* IO错误 */
    EG_ERR_OVERFLOW         = -10,      /* 溢出 */
    EG_ERR_UNDERFLOW        = -11,      /* 下溢 */
    EG_ERR_CRC              = -12,      /* CRC校验失败 */
    EG_ERR_PROTOCOL         = -13,      /* 协议错误 */
    EG_ERR_NETWORK          = -14,      /* 网络错误 */
    EG_ERR_HARDWARE         = -15,      /* 硬件错误 */
} eg_error_t;

/**
 * @brief 返回值检查宏
 */
#define EG_CHECK(expr) do { \
    eg_error_t _err = (expr); \
    if (EG_UNLIKELY(_err != EG_OK)) { \
        return _err; \
    } \
} while(0)

#define EG_CHECK_PTR(ptr) do { \
    if (EG_UNLIKELY((ptr) == NULL)) { \
        return EG_ERR_INVALID_PARAM; \
    } \
} while(0)

/*============================================================================
 *                              时间类型
 *===========================================================================*/

/**
 * @brief 时间戳 (毫秒)
 */
typedef u64 eg_time_ms_t;

/**
 * @brief 时间戳 (微秒)
 */
typedef u64 eg_time_us_t;

/**
 * @brief 时间间隔
 */
typedef struct eg_duration {
    u32 seconds;
    u32 nanoseconds;
} eg_duration_t;

/*============================================================================
 *                              缓冲区类型
 *===========================================================================*/

/**
 * @brief 字节缓冲区
 */
typedef struct eg_buffer {
    u8*     data;       /* 数据指针 */
    usize   size;       /* 有效数据长度 */
    usize   capacity;   /* 容量 */
} eg_buffer_t;

/**
 * @brief 常量缓冲区 (只读)
 */
typedef struct eg_const_buffer {
    const u8*   data;
    usize       size;
} eg_const_buffer_t;

/**
 * @brief 环形缓冲区
 */
typedef struct eg_ring_buffer {
    u8*     buffer;     /* 缓冲区 */
    usize   size;       /* 缓冲区大小 */
    usize   head;       /* 头指针 */
    usize   tail;       /* 尾指针 */
} eg_ring_buffer_t;

/*============================================================================
 *                              回调类型
 *===========================================================================*/

/**
 * @brief 通用回调函数
 */
typedef void (*eg_callback_fn)(void* arg);

/**
 * @brief 带返回值的回调
 */
typedef eg_error_t (*eg_handler_fn)(void* arg);

/**
 * @brief 数据处理回调
 */
typedef eg_error_t (*eg_data_handler_fn)(const u8* data, usize len, void* ctx);

/**
 * @brief 分配器接口
 */
typedef struct eg_allocator {
    void* (*alloc)(usize size, void* ctx);
    void  (*free)(void* ptr, void* ctx);
    void* ctx;
} eg_allocator_t;

/*============================================================================
 *                              版本信息
 *===========================================================================*/

#define EG_VERSION_MAJOR    1
#define EG_VERSION_MINOR    0
#define EG_VERSION_PATCH    0
#define EG_VERSION_STRING   "1.0.0"

#define EG_VERSION_NUMBER   ((EG_VERSION_MAJOR << 16) | \
                             (EG_VERSION_MINOR << 8) | \
                             EG_VERSION_PATCH)

#ifdef __cplusplus
}
#endif

#endif /* EDGE_GATEWAY_TYPES_H */
