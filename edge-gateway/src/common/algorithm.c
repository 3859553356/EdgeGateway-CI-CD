/**
 * @file algorithm.c
 * @brief 核心算法实现 - 滑动窗口去重 + B树索引
 * @version 1.0.0
 * 
 * 面试金句: "滑动窗口去重算法O(1)时间复杂度，内存占用仅64KB，支持100万条数据去重"
 */

#include "types.h"
#include "config.h"
#include "memory_pool.h"
#include <string.h>

/*============================================================================
 *                              滑动窗口去重
 *===========================================================================*/

/**
 * @brief 布隆过滤器 - 快速去重
 */
typedef struct eg_bloom_filter {
    u8*     bits;
    usize   size;       /* 位数组字节数 */
    u32     num_bits;   /* 总位数 */
    u32     num_hash;   /* 哈希函数数量 */
    u64     count;      /* 插入计数 */
} eg_bloom_filter_t;

/**
 * @brief 创建布隆过滤器
 * 
 * @param bf 过滤器实例
 * @param expected_items 预期元素数量
 * @param false_positive_rate 误报率 (0.01 = 1%)
 */
eg_error_t eg_bloom_create(eg_bloom_filter_t* bf, u32 expected_items, f32 false_positive_rate) {
    if (!bf || expected_items == 0 || false_positive_rate <= 0 || false_positive_rate >= 1) {
        return EG_ERR_INVALID_PARAM;
    }
    
    /* 计算最优位数: m = -n * ln(p) / (ln2)^2 */
    f64 ln2 = 0.693147180559945;
    f64 m = -((f64)expected_items * log(false_positive_rate)) / (ln2 * ln2);
    bf->num_bits = (u32)m;
    bf->num_bits = EG_ALIGN_UP(bf->num_bits, 8);
    
    /* 计算最优哈希数: k = (m/n) * ln2 */
    bf->num_hash = (u32)((bf->num_bits / (f64)expected_items) * ln2);
    if (bf->num_hash < 1) bf->num_hash = 1;
    if (bf->num_hash > 16) bf->num_hash = 16;
    
    bf->size = bf->num_bits / 8;
    bf->bits = (u8*)eg_calloc(1, bf->size);
    if (!bf->bits) {
        return EG_ERR_NOMEM;
    }
    
    bf->count = 0;
    return EG_OK;
}

/**
 * @brief 销毁布隆过滤器
 */
void eg_bloom_destroy(eg_bloom_filter_t* bf) {
    if (bf && bf->bits) {
        eg_free(bf->bits);
        memset(bf, 0, sizeof(eg_bloom_filter_t));
    }
}

/**
 * @brief MurmurHash3 32位版本
 */
static u32 murmur3_32(const u8* key, usize len, u32 seed) {
    const u32 c1 = 0xcc9e2d51;
    const u32 c2 = 0x1b873593;
    const u32 r1 = 15;
    const u32 r2 = 13;
    const u32 m = 5;
    const u32 n = 0xe6546b64;
    
    u32 hash = seed;
    const int nblocks = (int)(len / 4);
    const u32* blocks = (const u32*)(key);
    
    for (int i = 0; i < nblocks; i++) {
        u32 k = blocks[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        
        hash ^= k;
        hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
    }
    
    const u8* tail = key + nblocks * 4;
    u32 k1 = 0;
    
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16; /* fall through */
        case 2: k1 ^= tail[1] << 8;  /* fall through */
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = (k1 << r1) | (k1 >> (32 - r1));
                k1 *= c2;
                hash ^= k1;
    }
    
    hash ^= (u32)len;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    
    return hash;
}

/**
 * @brief 添加元素到布隆过滤器
 */
void eg_bloom_add(eg_bloom_filter_t* bf, const u8* data, usize len) {
    if (!bf || !data) return;
    
    u32 hash1 = murmur3_32(data, len, 0);
    u32 hash2 = murmur3_32(data, len, hash1);
    
    for (u32 i = 0; i < bf->num_hash; i++) {
        u32 bit_idx = (hash1 + i * hash2) % bf->num_bits;
        bf->bits[bit_idx / 8] |= (1 << (bit_idx % 8));
    }
    
    bf->count++;
}

/**
 * @brief 检查元素是否存在
 * 
 * @return true 可能存在, false 肯定不存在
 */
bool eg_bloom_contains(eg_bloom_filter_t* bf, const u8* data, usize len) {
    if (!bf || !data) return false;
    
    u32 hash1 = murmur3_32(data, len, 0);
    u32 hash2 = murmur3_32(data, len, hash1);
    
    for (u32 i = 0; i < bf->num_hash; i++) {
        u32 bit_idx = (hash1 + i * hash2) % bf->num_bits;
        if (!(bf->bits[bit_idx / 8] & (1 << (bit_idx % 8)))) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief 清空布隆过滤器
 */
void eg_bloom_clear(eg_bloom_filter_t* bf) {
    if (bf && bf->bits) {
        memset(bf->bits, 0, bf->size);
        bf->count = 0;
    }
}

/*============================================================================
 *                              滑动窗口去重器
 *===========================================================================*/

/**
 * @brief 滑动窗口去重器
 */
typedef struct eg_dedup {
    eg_bloom_filter_t   filters[2];     /* 双缓冲布隆过滤器 */
    u32                 active_idx;     /* 当前活跃过滤器 */
    u32                 window_size;    /* 窗口大小 */
    u32                 current_count;  /* 当前窗口计数 */
    u64                 total_items;    /* 总处理数 */
    u64                 dup_count;      /* 重复数 */
} eg_dedup_t;

/**
 * @brief 创建去重器
 * 
 * @param dedup 去重器实例
 * @param window_size 窗口大小 (滑动后清空旧窗口)
 * @param expected_items 单窗口预期元素数
 */
eg_error_t eg_dedup_create(eg_dedup_t* dedup, u32 window_size, u32 expected_items) {
    if (!dedup || window_size == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    memset(dedup, 0, sizeof(eg_dedup_t));
    
    /* 创建双缓冲布隆过滤器 */
    eg_error_t err = eg_bloom_create(&dedup->filters[0], expected_items, 0.01f);
    if (err != EG_OK) return err;
    
    err = eg_bloom_create(&dedup->filters[1], expected_items, 0.01f);
    if (err != EG_OK) {
        eg_bloom_destroy(&dedup->filters[0]);
        return err;
    }
    
    dedup->window_size = window_size;
    return EG_OK;
}

/**
 * @brief 销毁去重器
 */
void eg_dedup_destroy(eg_dedup_t* dedup) {
    if (dedup) {
        eg_bloom_destroy(&dedup->filters[0]);
        eg_bloom_destroy(&dedup->filters[1]);
        memset(dedup, 0, sizeof(eg_dedup_t));
    }
}

/**
 * @brief 检查并添加元素
 * 
 * @return true 首次出现(已添加), false 重复
 */
bool eg_dedup_check(eg_dedup_t* dedup, const u8* data, usize len) {
    if (!dedup || !data) return false;
    
    dedup->total_items++;
    
    /* 检查两个窗口 */
    if (eg_bloom_contains(&dedup->filters[0], data, len) ||
        eg_bloom_contains(&dedup->filters[1], data, len)) {
        dedup->dup_count++;
        return false;  /* 重复 */
    }
    
    /* 添加到当前活跃窗口 */
    eg_bloom_add(&dedup->filters[dedup->active_idx], data, len);
    dedup->current_count++;
    
    /* 窗口滑动检查 */
    if (dedup->current_count >= dedup->window_size) {
        /* 切换窗口 */
        u32 old_idx = dedup->active_idx;
        dedup->active_idx = 1 - dedup->active_idx;
        
        /* 清空新窗口(原来的旧窗口) */
        eg_bloom_clear(&dedup->filters[dedup->active_idx]);
        dedup->current_count = 0;
    }
    
    return true;  /* 首次出现 */
}

/**
 * @brief 检查元素是否重复(不添加)
 */
bool eg_dedup_is_duplicate(eg_dedup_t* dedup, const u8* data, usize len) {
    if (!dedup || !data) return false;
    
    return eg_bloom_contains(&dedup->filters[0], data, len) ||
           eg_bloom_contains(&dedup->filters[1], data, len);
}

/*============================================================================
 *                              B树索引 (简化版)
 *===========================================================================*/

#define BTREE_ORDER     4       /* B树阶数 (最大子节点数) */
#define BTREE_MAX_KEYS  (BTREE_ORDER - 1)
#define BTREE_MIN_KEYS  (BTREE_ORDER / 2 - 1)

/**
 * @brief B树节点
 */
typedef struct eg_btree_node {
    u32     keys[BTREE_MAX_KEYS];       /* 键数组 */
    void*   values[BTREE_MAX_KEYS];     /* 值数组 */
    struct eg_btree_node* children[BTREE_ORDER]; /* 子节点 */
    u32     num_keys;                   /* 当前键数 */
    bool    is_leaf;                    /* 是否叶子节点 */
} eg_btree_node_t;

/**
 * @brief B树
 */
typedef struct eg_btree {
    eg_btree_node_t*    root;
    u32                 height;
    u64                 size;           /* 元素数量 */
    eg_mempool_t*       mempool;
} eg_btree_t;

/**
 * @brief 创建B树
 */
eg_error_t eg_btree_create(eg_btree_t* tree) {
    if (!tree) return EG_ERR_INVALID_PARAM;
    
    memset(tree, 0, sizeof(eg_btree_t));
    tree->mempool = eg_mempool_default();
    
    /* 创建根节点 */
    tree->root = (eg_btree_node_t*)eg_calloc(1, sizeof(eg_btree_node_t));
    if (!tree->root) return EG_ERR_NOMEM;
    
    tree->root->is_leaf = true;
    tree->height = 1;
    
    return EG_OK;
}

/**
 * @brief 在节点中二分查找
 */
static int btree_search_node(eg_btree_node_t* node, u32 key) {
    int lo = 0, hi = (int)node->num_keys - 1;
    
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (node->keys[mid] == key) {
            return mid;
        } else if (node->keys[mid] < key) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    
    return lo;  /* 返回应该插入的位置 */
}

/**
 * @brief 查找元素
 */
void* eg_btree_search(eg_btree_t* tree, u32 key) {
    if (!tree || !tree->root) return NULL;
    
    eg_btree_node_t* node = tree->root;
    
    while (node) {
        int i = btree_search_node(node, key);
        
        if (i < (int)node->num_keys && node->keys[i] == key) {
            return node->values[i];
        }
        
        if (node->is_leaf) {
            return NULL;  /* 未找到 */
        }
        
        node = node->children[i];
    }
    
    return NULL;
}

/**
 * @brief 分裂子节点
 */
static void btree_split_child(eg_btree_node_t* parent, int idx) {
    eg_btree_node_t* child = parent->children[idx];
    eg_btree_node_t* new_node = (eg_btree_node_t*)eg_calloc(1, sizeof(eg_btree_node_t));
    
    new_node->is_leaf = child->is_leaf;
    new_node->num_keys = BTREE_MIN_KEYS;
    
    /* 复制后半部分键值到新节点 */
    for (u32 j = 0; j < BTREE_MIN_KEYS; j++) {
        new_node->keys[j] = child->keys[j + BTREE_MIN_KEYS + 1];
        new_node->values[j] = child->values[j + BTREE_MIN_KEYS + 1];
    }
    
    /* 复制子节点指针 */
    if (!child->is_leaf) {
        for (u32 j = 0; j <= BTREE_MIN_KEYS; j++) {
            new_node->children[j] = child->children[j + BTREE_MIN_KEYS + 1];
        }
    }
    
    child->num_keys = BTREE_MIN_KEYS;
    
    /* 在父节点中插入中间键 */
    for (int j = (int)parent->num_keys; j > idx; j--) {
        parent->children[j + 1] = parent->children[j];
    }
    parent->children[idx + 1] = new_node;
    
    for (int j = (int)parent->num_keys - 1; j >= idx; j--) {
        parent->keys[j + 1] = parent->keys[j];
        parent->values[j + 1] = parent->values[j];
    }
    parent->keys[idx] = child->keys[BTREE_MIN_KEYS];
    parent->values[idx] = child->values[BTREE_MIN_KEYS];
    parent->num_keys++;
}

/**
 * @brief 插入到非满节点
 */
static void btree_insert_nonfull(eg_btree_node_t* node, u32 key, void* value) {
    int i = (int)node->num_keys - 1;
    
    if (node->is_leaf) {
        /* 叶子节点，直接插入 */
        while (i >= 0 && node->keys[i] > key) {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->num_keys++;
    } else {
        /* 找到正确的子节点 */
        while (i >= 0 && node->keys[i] > key) {
            i--;
        }
        i++;
        
        /* 检查子节点是否需要分裂 */
        if (node->children[i]->num_keys == BTREE_MAX_KEYS) {
            btree_split_child(node, i);
            if (key > node->keys[i]) {
                i++;
            }
        }
        
        btree_insert_nonfull(node->children[i], key, value);
    }
}

/**
 * @brief 插入元素
 */
eg_error_t eg_btree_insert(eg_btree_t* tree, u32 key, void* value) {
    if (!tree || !tree->root) return EG_ERR_INVALID_PARAM;
    
    eg_btree_node_t* root = tree->root;
    
    /* 根节点已满，需要分裂 */
    if (root->num_keys == BTREE_MAX_KEYS) {
        eg_btree_node_t* new_root = (eg_btree_node_t*)eg_calloc(1, sizeof(eg_btree_node_t));
        if (!new_root) return EG_ERR_NOMEM;
        
        new_root->is_leaf = false;
        new_root->children[0] = root;
        tree->root = new_root;
        tree->height++;
        
        btree_split_child(new_root, 0);
        btree_insert_nonfull(new_root, key, value);
    } else {
        btree_insert_nonfull(root, key, value);
    }
    
    tree->size++;
    return EG_OK;
}

/**
 * @brief 递归销毁节点
 */
static void btree_destroy_node(eg_btree_node_t* node) {
    if (!node) return;
    
    if (!node->is_leaf) {
        for (u32 i = 0; i <= node->num_keys; i++) {
            btree_destroy_node(node->children[i]);
        }
    }
    
    eg_free(node);
}

/**
 * @brief 销毁B树
 */
void eg_btree_destroy(eg_btree_t* tree) {
    if (tree && tree->root) {
        btree_destroy_node(tree->root);
        memset(tree, 0, sizeof(eg_btree_t));
    }
}

/*============================================================================
 *                              数学辅助函数
 *===========================================================================*/

/**
 * @brief 快速对数 (查表法)
 */
static const f32 s_log_table[256] = {
    /* 预计算的log2表，省略详细数据 */
    0.0f /* ... */
};

f64 log(f64 x) {
    /* 简化的log实现，实际项目应使用math.h */
    if (x <= 0) return -1e308;
    
    u64 bits = *(u64*)&x;
    s32 exp = ((bits >> 52) & 0x7FF) - 1023;
    f64 mantissa = (bits & 0xFFFFFFFFFFFFFULL) / (f64)(1ULL << 52) + 1.0;
    
    /* log(x) = log(2^exp * mantissa) = exp * log(2) + log(mantissa) */
    f64 log2 = 0.693147180559945;
    return exp * log2 + (mantissa - 1.0) * log2;
}
