/**
 * @file test_memory_pool.c
 * @brief 内存池单元测试
 * @version 1.0.0
 */

#include "unity.h"
#include "memory_pool.h"
#include <string.h>

/*============================================================================
 *                              测试夹具
 *===========================================================================*/

static eg_mempool_t test_pool;
static u8 test_buffer[64 * 1024];  /* 64KB测试缓冲 */

void setUp(void) {
    memset(&test_pool, 0, sizeof(test_pool));
    memset(test_buffer, 0, sizeof(test_buffer));
    eg_mempool_create(&test_pool, test_buffer, sizeof(test_buffer), 8);
}

void tearDown(void) {
    eg_mempool_destroy(&test_pool);
}

/*============================================================================
 *                              测试用例
 *===========================================================================*/

/**
 * @brief 测试内存池创建
 */
void test_mempool_create(void) {
    TEST_ASSERT_TRUE(test_pool.initialized);
    TEST_ASSERT_EQUAL_PTR(test_buffer, test_pool.pool_start);
    TEST_ASSERT_EQUAL(sizeof(test_buffer), test_pool.pool_size);
}

/**
 * @brief 测试基本分配
 */
void test_mempool_alloc_basic(void) {
    void* ptr = eg_mempool_alloc(&test_pool, 100);
    TEST_ASSERT_NOT_NULL(ptr);
    
    /* 验证对齐 */
    TEST_ASSERT_EQUAL(0, (uptr)ptr % 8);
    
    /* 可以写入 */
    memset(ptr, 0xAB, 100);
    
    eg_mempool_free(&test_pool, ptr);
}

/**
 * @brief 测试多次分配
 */
void test_mempool_alloc_multiple(void) {
    void* ptrs[100];
    
    /* 分配100个小块 */
    for (int i = 0; i < 100; i++) {
        ptrs[i] = eg_mempool_alloc(&test_pool, 32);
        TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], "Allocation failed");
    }
    
    /* 释放所有块 */
    for (int i = 0; i < 100; i++) {
        eg_mempool_free(&test_pool, ptrs[i]);
    }
}

/**
 * @brief 测试不同大小分配
 */
void test_mempool_alloc_various_sizes(void) {
    void* p1 = eg_mempool_alloc(&test_pool, 16);
    void* p2 = eg_mempool_alloc(&test_pool, 64);
    void* p3 = eg_mempool_alloc(&test_pool, 256);
    void* p4 = eg_mempool_alloc(&test_pool, 1024);
    
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_NOT_NULL(p3);
    TEST_ASSERT_NOT_NULL(p4);
    
    /* 指针不应重叠 */
    TEST_ASSERT_NOT_EQUAL(p1, p2);
    TEST_ASSERT_NOT_EQUAL(p2, p3);
    TEST_ASSERT_NOT_EQUAL(p3, p4);
    
    eg_mempool_free(&test_pool, p1);
    eg_mempool_free(&test_pool, p2);
    eg_mempool_free(&test_pool, p3);
    eg_mempool_free(&test_pool, p4);
}

/**
 * @brief 测试calloc (分配并清零)
 */
void test_mempool_calloc(void) {
    u8* ptr = (u8*)eg_mempool_calloc(&test_pool, 10, 10);
    TEST_ASSERT_NOT_NULL(ptr);
    
    /* 验证已清零 */
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL(0, ptr[i]);
    }
    
    eg_mempool_free(&test_pool, ptr);
}

/**
 * @brief 测试realloc
 */
void test_mempool_realloc(void) {
    /* 分配初始块 */
    u8* ptr = (u8*)eg_mempool_alloc(&test_pool, 32);
    TEST_ASSERT_NOT_NULL(ptr);
    
    /* 写入数据 */
    for (int i = 0; i < 32; i++) {
        ptr[i] = (u8)i;
    }
    
    /* 扩大 */
    u8* new_ptr = (u8*)eg_mempool_realloc(&test_pool, ptr, 64);
    TEST_ASSERT_NOT_NULL(new_ptr);
    
    /* 验证原数据保留 */
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL(i, new_ptr[i]);
    }
    
    eg_mempool_free(&test_pool, new_ptr);
}

/**
 * @brief 测试realloc NULL
 */
void test_mempool_realloc_null(void) {
    /* realloc(NULL) 应该等同于 malloc */
    void* ptr = eg_mempool_realloc(&test_pool, NULL, 32);
    TEST_ASSERT_NOT_NULL(ptr);
    eg_mempool_free(&test_pool, ptr);
}

/**
 * @brief 测试free NULL
 */
void test_mempool_free_null(void) {
    /* free(NULL) 应该安全 */
    eg_mempool_free(&test_pool, NULL);
    TEST_PASS();
}

/**
 * @brief 测试统计信息
 */
void test_mempool_stats(void) {
    eg_mempool_stats_t stats;
    
    /* 分配前 */
    eg_mempool_get_stats(&test_pool, &stats);
    u64 initial_free = stats.free_size;
    
    /* 分配一些内存 */
    void* p1 = eg_mempool_alloc(&test_pool, 256);
    void* p2 = eg_mempool_alloc(&test_pool, 512);
    
    /* 分配后 */
    eg_mempool_get_stats(&test_pool, &stats);
    TEST_ASSERT_LESS_THAN(initial_free, stats.free_size);
    TEST_ASSERT_EQUAL(2, stats.alloc_count);
    
    eg_mempool_free(&test_pool, p1);
    eg_mempool_free(&test_pool, p2);
}

/**
 * @brief 测试内存池检查
 */
void test_mempool_check(void) {
    /* 正常状态下检查应通过 */
    TEST_ASSERT_EQUAL(EG_OK, eg_mempool_check(&test_pool));
    
    /* 分配和释放后检查 */
    void* ptr = eg_mempool_alloc(&test_pool, 128);
    TEST_ASSERT_EQUAL(EG_OK, eg_mempool_check(&test_pool));
    
    eg_mempool_free(&test_pool, ptr);
    TEST_ASSERT_EQUAL(EG_OK, eg_mempool_check(&test_pool));
}

/*============================================================================
 *                              对象池测试
 *===========================================================================*/

typedef struct {
    int id;
    char name[32];
} test_object_t;

static eg_object_pool_t obj_pool;

void test_objpool_basic(void) {
    eg_error_t err = eg_objpool_create(&obj_pool, &test_pool, 
                                        sizeof(test_object_t), 10);
    TEST_ASSERT_EQUAL(EG_OK, err);
    
    /* 获取对象 */
    test_object_t* obj = (test_object_t*)eg_objpool_acquire(&obj_pool);
    TEST_ASSERT_NOT_NULL(obj);
    
    obj->id = 42;
    strcpy(obj->name, "test");
    
    /* 归还对象 */
    eg_objpool_release(&obj_pool, obj);
    
    /* 再次获取应该得到同一个对象 */
    test_object_t* obj2 = (test_object_t*)eg_objpool_acquire(&obj_pool);
    TEST_ASSERT_EQUAL_PTR(obj, obj2);
    
    eg_objpool_release(&obj_pool, obj2);
    eg_objpool_destroy(&obj_pool);
}

void test_objpool_exhaustion(void) {
    eg_objpool_create(&obj_pool, &test_pool, sizeof(test_object_t), 3);
    
    void* objs[5];
    
    /* 分配所有对象 */
    objs[0] = eg_objpool_acquire(&obj_pool);
    objs[1] = eg_objpool_acquire(&obj_pool);
    objs[2] = eg_objpool_acquire(&obj_pool);
    
    TEST_ASSERT_NOT_NULL(objs[0]);
    TEST_ASSERT_NOT_NULL(objs[1]);
    TEST_ASSERT_NOT_NULL(objs[2]);
    
    /* 池耗尽 */
    objs[3] = eg_objpool_acquire(&obj_pool);
    TEST_ASSERT_NULL(objs[3]);
    
    /* 释放一个后可以再分配 */
    eg_objpool_release(&obj_pool, objs[0]);
    objs[4] = eg_objpool_acquire(&obj_pool);
    TEST_ASSERT_NOT_NULL(objs[4]);
    
    eg_objpool_release(&obj_pool, objs[1]);
    eg_objpool_release(&obj_pool, objs[2]);
    eg_objpool_release(&obj_pool, objs[4]);
    eg_objpool_destroy(&obj_pool);
}

/*============================================================================
 *                              测试入口
 *===========================================================================*/

int main(void) {
    UNITY_BEGIN();
    
    /* 内存池测试 */
    RUN_TEST(test_mempool_create);
    RUN_TEST(test_mempool_alloc_basic);
    RUN_TEST(test_mempool_alloc_multiple);
    RUN_TEST(test_mempool_alloc_various_sizes);
    RUN_TEST(test_mempool_calloc);
    RUN_TEST(test_mempool_realloc);
    RUN_TEST(test_mempool_realloc_null);
    RUN_TEST(test_mempool_free_null);
    RUN_TEST(test_mempool_stats);
    RUN_TEST(test_mempool_check);
    
    /* 对象池测试 */
    RUN_TEST(test_objpool_basic);
    RUN_TEST(test_objpool_exhaustion);
    
    return UNITY_END();
}
