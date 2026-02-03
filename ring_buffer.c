#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define CACHE_LINE 64

typedef struct {
    uint8_t data[BUFFER_SIZE];
    
    alignas(CACHE_LINE) atomic_size_t head;
    alignas(CACHE_LINE) atomic_size_t tail;
} ring_buffer_t;

bool ring_push(ring_buffer_t *rb, uint8_t *src, size_t len) {
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    
    size_t available = (tail - head - 1) & (BUFFER_SIZE - 1);
    if (len > available) return false;
    
    // Zero-copy: write directly into the ring buffer
    for (size_t i = 0; i < len; i++) {
        rb->data[(head + i) & (BUFFER_SIZE - 1)] = src[i];
    }
    
    atomic_store_explicit(&rb->head, 
                         (head + len) & (BUFFER_SIZE - 1), 
                         memory_order_release);
    return true;
}

bool ring_pop(ring_buffer_t *rb, uint8_t *dst, size_t len) {
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    
    size_t available = (head - tail) & (BUFFER_SIZE - 1);
    if (len > available) return false;
    
    // Zero-copy: read directly from ring buffer
    for (size_t i = 0; i < len; i++) {
        dst[i] = rb->data[(tail + i) & (BUFFER_SIZE - 1)];
    }
    
    atomic_store_explicit(&rb->tail, 
                         (tail + len) & (BUFFER_SIZE - 1), 
                         memory_order_release);
    return true;
}
