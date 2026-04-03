/**
 * @file lock_free_queue.c
 * @brief 无锁队列实现
 * @version 1.0.0
 */

#include "lock_free_queue.h"
#include "memory_pool.h"
#include <string.h>

/*============================================================================
 *                              SPSC队列实现
 *===========================================================================*/

eg_error_t eg_spsc_queue_create(eg_spsc_queue_t* queue, u64 capacity) {
    if (!queue || capacity == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    /* 确保容量是2的幂 */
    if (!EG_IS_POWER_OF_2(capacity)) {
        capacity = 1ULL << (64 - __builtin_clzll(capacity));
    }
    
    memset(queue, 0, sizeof(eg_spsc_queue_t));
    
    queue->buffer = (void**)eg_malloc(capacity * sizeof(void*));
    if (!queue->buffer) {
        return EG_ERR_NOMEM;
    }
    
    memset(queue->buffer, 0, capacity * sizeof(void*));
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    
    return EG_OK;
}

void eg_spsc_queue_destroy(eg_spsc_queue_t* queue) {
    if (queue && queue->buffer) {
        eg_free(queue->buffer);
        memset(queue, 0, sizeof(eg_spsc_queue_t));
    }
}

bool eg_spsc_queue_enqueue(eg_spsc_queue_t* queue, void* item) {
    u64 head = queue->head;
    u64 next_head = head + 1;
    
    /* 检查是否已满 (使用缓存的tail位置) */
    if (next_head - queue->tail_cache > queue->capacity) {
        /* 更新缓存 */
        queue->tail_cache = eg_atomic_load(&queue->tail);
        if (next_head - queue->tail_cache > queue->capacity) {
            queue->full_count++;
            return false;
        }
    }
    
    queue->buffer[head & queue->mask] = item;
    eg_write_barrier();
    eg_atomic_store(&queue->head, next_head);
    queue->enqueue_count++;
    
    return true;
}

u64 eg_spsc_queue_enqueue_bulk(eg_spsc_queue_t* queue, void** items, u64 count) {
    u64 head = queue->head;
    
    /* 计算可用空间 */
    u64 tail = queue->tail_cache;
    u64 available = queue->capacity - (head - tail);
    
    if (available < count) {
        tail = eg_atomic_load(&queue->tail);
        queue->tail_cache = tail;
        available = queue->capacity - (head - tail);
    }
    
    if (count > available) {
        count = available;
    }
    
    if (count == 0) {
        return 0;
    }
    
    /* 批量写入 */
    for (u64 i = 0; i < count; i++) {
        queue->buffer[(head + i) & queue->mask] = items[i];
    }
    
    eg_write_barrier();
    eg_atomic_store(&queue->head, head + count);
    queue->enqueue_count += count;
    
    return count;
}

bool eg_spsc_queue_dequeue(eg_spsc_queue_t* queue, void** item) {
    u64 tail = queue->tail;
    
    /* 检查是否为空 (使用缓存的head位置) */
    if (tail >= queue->head_cache) {
        queue->head_cache = eg_atomic_load(&queue->head);
        if (tail >= queue->head_cache) {
            queue->empty_count++;
            return false;
        }
    }
    
    eg_read_barrier();
    *item = queue->buffer[tail & queue->mask];
    eg_atomic_store(&queue->tail, tail + 1);
    queue->dequeue_count++;
    
    return true;
}

u64 eg_spsc_queue_dequeue_bulk(eg_spsc_queue_t* queue, void** items, u64 max_count) {
    u64 tail = queue->tail;
    u64 head = queue->head_cache;
    
    if (tail >= head) {
        head = eg_atomic_load(&queue->head);
        queue->head_cache = head;
    }
    
    u64 available = head - tail;
    if (max_count > available) {
        max_count = available;
    }
    
    if (max_count == 0) {
        return 0;
    }
    
    eg_read_barrier();
    
    for (u64 i = 0; i < max_count; i++) {
        items[i] = queue->buffer[(tail + i) & queue->mask];
    }
    
    eg_atomic_store(&queue->tail, tail + max_count);
    queue->dequeue_count += max_count;
    
    return max_count;
}

bool eg_spsc_queue_peek(eg_spsc_queue_t* queue, void** item) {
    u64 tail = queue->tail;
    
    if (tail >= queue->head_cache) {
        queue->head_cache = eg_atomic_load(&queue->head);
        if (tail >= queue->head_cache) {
            return false;
        }
    }
    
    eg_read_barrier();
    *item = queue->buffer[tail & queue->mask];
    return true;
}

/*============================================================================
 *                              MPMC队列实现
 *===========================================================================*/

eg_error_t eg_mpmc_queue_create(eg_mpmc_queue_t* queue, u64 capacity) {
    if (!queue || capacity == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    /* 确保容量是2的幂 */
    if (!EG_IS_POWER_OF_2(capacity)) {
        capacity = 1ULL << (64 - __builtin_clzll(capacity));
    }
    
    memset(queue, 0, sizeof(eg_mpmc_queue_t));
    
    queue->buffer = (eg_mpmc_node_t*)eg_malloc(capacity * sizeof(eg_mpmc_node_t));
    if (!queue->buffer) {
        return EG_ERR_NOMEM;
    }
    
    /* 初始化序列号 */
    for (u64 i = 0; i < capacity; i++) {
        queue->buffer[i].sequence = i;
        queue->buffer[i].data = NULL;
    }
    
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    
    return EG_OK;
}

void eg_mpmc_queue_destroy(eg_mpmc_queue_t* queue) {
    if (queue && queue->buffer) {
        eg_free(queue->buffer);
        memset(queue, 0, sizeof(eg_mpmc_queue_t));
    }
}

bool eg_mpmc_queue_enqueue(eg_mpmc_queue_t* queue, void* item) {
    eg_mpmc_node_t* node;
    u64 pos;
    
    for (;;) {
        pos = eg_atomic_load(&queue->head);
        node = &queue->buffer[pos & queue->mask];
        u64 seq = eg_atomic_load(&node->sequence);
        s64 diff = (s64)seq - (s64)pos;
        
        if (diff == 0) {
            /* 尝试占用此位置 */
            if (eg_atomic_cas(&queue->head, &pos, pos + 1)) {
                break;
            }
        } else if (diff < 0) {
            /* 队列已满 */
            return false;
        }
        /* diff > 0: 其他线程正在操作，重试 */
    }
    
    node->data = item;
    eg_atomic_store(&node->sequence, pos + 1);
    eg_atomic_add(&queue->enqueue_count, 1);
    
    return true;
}

bool eg_mpmc_queue_dequeue(eg_mpmc_queue_t* queue, void** item) {
    eg_mpmc_node_t* node;
    u64 pos;
    
    for (;;) {
        pos = eg_atomic_load(&queue->tail);
        node = &queue->buffer[pos & queue->mask];
        u64 seq = eg_atomic_load(&node->sequence);
        s64 diff = (s64)seq - (s64)(pos + 1);
        
        if (diff == 0) {
            /* 尝试消费此位置 */
            if (eg_atomic_cas(&queue->tail, &pos, pos + 1)) {
                break;
            }
        } else if (diff < 0) {
            /* 队列为空 */
            return false;
        }
        /* diff > 0: 其他线程正在操作，重试 */
    }
    
    *item = node->data;
    eg_atomic_store(&node->sequence, pos + queue->capacity);
    eg_atomic_add(&queue->dequeue_count, 1);
    
    return true;
}

/*============================================================================
 *                              工作窃取队列实现
 *===========================================================================*/

eg_error_t eg_wsqueue_create(eg_wsqueue_t* queue, u64 capacity) {
    if (!queue || capacity == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    if (!EG_IS_POWER_OF_2(capacity)) {
        capacity = 1ULL << (64 - __builtin_clzll(capacity));
    }
    
    memset(queue, 0, sizeof(eg_wsqueue_t));
    
    queue->buffer = (void**)eg_malloc(capacity * sizeof(void*));
    if (!queue->buffer) {
        return EG_ERR_NOMEM;
    }
    
    memset(queue->buffer, 0, capacity * sizeof(void*));
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    
    return EG_OK;
}

void eg_wsqueue_destroy(eg_wsqueue_t* queue) {
    if (queue && queue->buffer) {
        eg_free(queue->buffer);
        memset(queue, 0, sizeof(eg_wsqueue_t));
    }
}

bool eg_wsqueue_push(eg_wsqueue_t* queue, void* item) {
    s64 bottom = eg_atomic_load(&queue->bottom);
    s64 top = eg_atomic_load(&queue->top);
    
    if ((u64)(bottom - top) >= queue->capacity) {
        return false;  /* 队列满 */
    }
    
    queue->buffer[bottom & queue->mask] = item;
    eg_write_barrier();
    eg_atomic_store(&queue->bottom, bottom + 1);
    
    return true;
}

bool eg_wsqueue_pop(eg_wsqueue_t* queue, void** item) {
    s64 bottom = eg_atomic_load(&queue->bottom) - 1;
    eg_atomic_store(&queue->bottom, bottom);
    eg_memory_barrier();
    
    s64 top = eg_atomic_load(&queue->top);
    
    if (top <= bottom) {
        *item = queue->buffer[bottom & queue->mask];
        
        if (top == bottom) {
            /* 最后一个元素，可能有竞争 */
            s64 expected = top;
            if (!eg_atomic_cas(&queue->top, &expected, top + 1)) {
                /* 被偷走了 */
                eg_atomic_store(&queue->bottom, top + 1);
                return false;
            }
            eg_atomic_store(&queue->bottom, top + 1);
        }
        return true;
    } else {
        /* 队列为空 */
        eg_atomic_store(&queue->bottom, top);
        return false;
    }
}

bool eg_wsqueue_steal(eg_wsqueue_t* queue, void** item) {
    s64 top = eg_atomic_load(&queue->top);
    eg_read_barrier();
    s64 bottom = eg_atomic_load(&queue->bottom);
    
    if (top < bottom) {
        *item = queue->buffer[top & queue->mask];
        
        s64 expected = top;
        if (eg_atomic_cas(&queue->top, &expected, top + 1)) {
            return true;
        }
    }
    
    return false;
}

/*============================================================================
 *                              环形缓冲区实现
 *===========================================================================*/

eg_error_t eg_ring_buf_create(eg_ring_buf_t* ring, usize capacity) {
    if (!ring || capacity == 0) {
        return EG_ERR_INVALID_PARAM;
    }
    
    if (!EG_IS_POWER_OF_2(capacity)) {
        capacity = 1UL << (sizeof(usize) * 8 - __builtin_clzl(capacity));
    }
    
    memset(ring, 0, sizeof(eg_ring_buf_t));
    
    ring->buffer = (u8*)eg_malloc(capacity);
    if (!ring->buffer) {
        return EG_ERR_NOMEM;
    }
    
    ring->capacity = capacity;
    return EG_OK;
}

void eg_ring_buf_destroy(eg_ring_buf_t* ring) {
    if (ring && ring->buffer) {
        eg_free(ring->buffer);
        memset(ring, 0, sizeof(eg_ring_buf_t));
    }
}

usize eg_ring_buf_write(eg_ring_buf_t* ring, const u8* data, usize len) {
    usize writable = eg_ring_buf_writable(ring);
    if (len > writable) {
        len = writable;
    }
    
    if (len == 0) {
        return 0;
    }
    
    usize write_pos = ring->write_pos;
    usize mask = ring->capacity - 1;
    
    /* 分两段写入 (环形缓冲区可能跨边界) */
    usize first_part = ring->capacity - (write_pos & mask);
    if (first_part > len) {
        first_part = len;
    }
    
    memcpy(ring->buffer + (write_pos & mask), data, first_part);
    if (len > first_part) {
        memcpy(ring->buffer, data + first_part, len - first_part);
    }
    
    eg_write_barrier();
    eg_atomic_store(&ring->write_pos, write_pos + len);
    
    return len;
}

usize eg_ring_buf_read(eg_ring_buf_t* ring, u8* data, usize len) {
    usize readable = eg_ring_buf_readable(ring);
    if (len > readable) {
        len = readable;
    }
    
    if (len == 0) {
        return 0;
    }
    
    usize read_pos = ring->read_pos;
    usize mask = ring->capacity - 1;
    
    usize first_part = ring->capacity - (read_pos & mask);
    if (first_part > len) {
        first_part = len;
    }
    
    memcpy(data, ring->buffer + (read_pos & mask), first_part);
    if (len > first_part) {
        memcpy(data + first_part, ring->buffer, len - first_part);
    }
    
    eg_atomic_store(&ring->read_pos, read_pos + len);
    
    return len;
}

usize eg_ring_buf_peek(eg_ring_buf_t* ring, u8* data, usize len) {
    usize readable = eg_ring_buf_readable(ring);
    if (len > readable) {
        len = readable;
    }
    
    if (len == 0) {
        return 0;
    }
    
    usize read_pos = ring->read_pos;
    usize mask = ring->capacity - 1;
    
    usize first_part = ring->capacity - (read_pos & mask);
    if (first_part > len) {
        first_part = len;
    }
    
    memcpy(data, ring->buffer + (read_pos & mask), first_part);
    if (len > first_part) {
        memcpy(data + first_part, ring->buffer, len - first_part);
    }
    
    return len;
}

void eg_ring_buf_skip(eg_ring_buf_t* ring, usize len) {
    usize readable = eg_ring_buf_readable(ring);
    if (len > readable) {
        len = readable;
    }
    
    eg_atomic_store(&ring->read_pos, ring->read_pos + len);
}
