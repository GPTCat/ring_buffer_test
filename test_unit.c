/*
 * Unit tests for ring buffer
 * Tests single-threaded correctness of push/pop operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ring_buffer.c"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    test_##name(); \
    printf(" PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_TRUE(x) ASSERT(x)
#define ASSERT_FALSE(x) ASSERT(!(x))

/* Helper to initialize buffer */
static void init_buffer(ring_buffer_t *rb) {
    memset(rb, 0, sizeof(*rb));
    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
}

/* ============ Basic Operations ============ */

TEST(empty_buffer_pop_fails) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t buf[10];
    ASSERT_FALSE(ring_pop(&rb, buf, 1));
    ASSERT_FALSE(ring_pop(&rb, buf, 10));
}

TEST(push_single_byte) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t data = 0x42;
    ASSERT_TRUE(ring_push(&rb, &data, 1));

    uint8_t out = 0;
    ASSERT_TRUE(ring_pop(&rb, &out, 1));
    ASSERT_EQ(out, 0x42);
}

TEST(push_pop_multiple_bytes) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t data[] = {1, 2, 3, 4, 5};
    ASSERT_TRUE(ring_push(&rb, data, 5));

    uint8_t out[5] = {0};
    ASSERT_TRUE(ring_pop(&rb, out, 5));

    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(out[i], data[i]);
    }
}

TEST(push_pop_sequence) {
    ring_buffer_t rb;
    init_buffer(&rb);

    for (int i = 0; i < 100; i++) {
        uint8_t val = (uint8_t)i;
        ASSERT_TRUE(ring_push(&rb, &val, 1));

        uint8_t out;
        ASSERT_TRUE(ring_pop(&rb, &out, 1));
        ASSERT_EQ(out, val);
    }
}

/* ============ Capacity Tests ============ */

TEST(max_capacity_is_buffer_size_minus_one) {
    ring_buffer_t rb;
    init_buffer(&rb);

    /* Buffer can hold BUFFER_SIZE - 1 bytes (one slot reserved) */
    uint8_t data[BUFFER_SIZE];
    memset(data, 0xAA, sizeof(data));

    /* Should fail for full BUFFER_SIZE */
    ASSERT_FALSE(ring_push(&rb, data, BUFFER_SIZE));

    /* Should succeed for BUFFER_SIZE - 1 */
    ASSERT_TRUE(ring_push(&rb, data, BUFFER_SIZE - 1));

    /* Buffer is now full, even 1 byte should fail */
    ASSERT_FALSE(ring_push(&rb, data, 1));
}

TEST(fill_and_drain) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t data[BUFFER_SIZE - 1];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    ASSERT_TRUE(ring_push(&rb, data, sizeof(data)));

    uint8_t out[BUFFER_SIZE - 1];
    ASSERT_TRUE(ring_pop(&rb, out, sizeof(out)));

    ASSERT_EQ(memcmp(data, out, sizeof(data)), 0);
}

TEST(partial_pop) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    ASSERT_TRUE(ring_push(&rb, data, 10));

    uint8_t out[5];
    ASSERT_TRUE(ring_pop(&rb, out, 5));
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(out[i], i + 1);
    }

    ASSERT_TRUE(ring_pop(&rb, out, 5));
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(out[i], i + 6);
    }
}

TEST(pop_more_than_available_fails) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t data[] = {1, 2, 3};
    ASSERT_TRUE(ring_push(&rb, data, 3));

    uint8_t out[10];
    ASSERT_FALSE(ring_pop(&rb, out, 10));

    /* Data should still be there */
    ASSERT_TRUE(ring_pop(&rb, out, 3));
}

/* ============ Wraparound Tests ============ */

TEST(wraparound_basic) {
    ring_buffer_t rb;
    init_buffer(&rb);

    /* Fill most of the buffer */
    uint8_t fill[BUFFER_SIZE - 100];
    memset(fill, 0x11, sizeof(fill));
    ASSERT_TRUE(ring_push(&rb, fill, sizeof(fill)));

    /* Drain it */
    uint8_t drain[BUFFER_SIZE - 100];
    ASSERT_TRUE(ring_pop(&rb, drain, sizeof(drain)));

    /* Now push data that wraps around */
    uint8_t wrap_data[200];
    for (int i = 0; i < 200; i++) {
        wrap_data[i] = (uint8_t)i;
    }
    ASSERT_TRUE(ring_push(&rb, wrap_data, 200));

    /* Pop and verify */
    uint8_t wrap_out[200];
    ASSERT_TRUE(ring_pop(&rb, wrap_out, 200));
    ASSERT_EQ(memcmp(wrap_data, wrap_out, 200), 0);
}

TEST(wraparound_multiple_cycles) {
    ring_buffer_t rb;
    init_buffer(&rb);

    /* Do multiple full cycles to stress wraparound */
    for (int cycle = 0; cycle < 10; cycle++) {
        uint8_t data[BUFFER_SIZE - 1];
        for (size_t i = 0; i < sizeof(data); i++) {
            data[i] = (uint8_t)((cycle + i) & 0xFF);
        }

        ASSERT_TRUE(ring_push(&rb, data, sizeof(data)));

        uint8_t out[BUFFER_SIZE - 1];
        ASSERT_TRUE(ring_pop(&rb, out, sizeof(out)));

        ASSERT_EQ(memcmp(data, out, sizeof(data)), 0);
    }
}

TEST(small_chunks_wraparound) {
    ring_buffer_t rb;
    init_buffer(&rb);

    /* Push/pop many small chunks to force wraparound */
    for (int i = 0; i < 5000; i++) {
        uint8_t data[7] = {(uint8_t)i, (uint8_t)(i+1), (uint8_t)(i+2),
                          (uint8_t)(i+3), (uint8_t)(i+4), (uint8_t)(i+5), (uint8_t)(i+6)};
        ASSERT_TRUE(ring_push(&rb, data, 7));

        uint8_t out[7];
        ASSERT_TRUE(ring_pop(&rb, out, 7));
        ASSERT_EQ(memcmp(data, out, 7), 0);
    }
}

/* ============ Edge Cases ============ */

TEST(zero_length_push) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t data = 0x42;
    ASSERT_TRUE(ring_push(&rb, &data, 0));  /* Should succeed (no-op) */
}

TEST(zero_length_pop) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint8_t out;
    ASSERT_TRUE(ring_pop(&rb, &out, 0));  /* Should succeed (no-op) */
}

TEST(alternating_push_pop) {
    ring_buffer_t rb;
    init_buffer(&rb);

    for (int i = 0; i < 1000; i++) {
        uint8_t val = (uint8_t)(i & 0xFF);
        ASSERT_TRUE(ring_push(&rb, &val, 1));

        uint8_t out;
        ASSERT_TRUE(ring_pop(&rb, &out, 1));
        ASSERT_EQ(out, val);
    }
}

/* ============ Data Integrity ============ */

TEST(data_pattern_integrity) {
    ring_buffer_t rb;
    init_buffer(&rb);

    /* Create pattern that's easy to verify */
    uint8_t pattern[256];
    for (int i = 0; i < 256; i++) {
        pattern[i] = (uint8_t)i;
    }

    ASSERT_TRUE(ring_push(&rb, pattern, 256));

    uint8_t out[256];
    ASSERT_TRUE(ring_pop(&rb, out, 256));

    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(out[i], (uint8_t)i);
    }
}

TEST(fifo_order_preserved) {
    ring_buffer_t rb;
    init_buffer(&rb);

    /* Push several chunks */
    uint8_t chunk1[] = {1, 2, 3};
    uint8_t chunk2[] = {4, 5, 6};
    uint8_t chunk3[] = {7, 8, 9};

    ASSERT_TRUE(ring_push(&rb, chunk1, 3));
    ASSERT_TRUE(ring_push(&rb, chunk2, 3));
    ASSERT_TRUE(ring_push(&rb, chunk3, 3));

    /* Pop all at once - should be in order */
    uint8_t out[9];
    ASSERT_TRUE(ring_pop(&rb, out, 9));

    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(out[i], i + 1);
    }
}

int main(void) {
    printf("Running unit tests...\n\n");
    printf("Basic Operations:\n");
    RUN_TEST(empty_buffer_pop_fails);
    RUN_TEST(push_single_byte);
    RUN_TEST(push_pop_multiple_bytes);
    RUN_TEST(push_pop_sequence);

    printf("\nCapacity Tests:\n");
    RUN_TEST(max_capacity_is_buffer_size_minus_one);
    RUN_TEST(fill_and_drain);
    RUN_TEST(partial_pop);
    RUN_TEST(pop_more_than_available_fails);

    printf("\nWraparound Tests:\n");
    RUN_TEST(wraparound_basic);
    RUN_TEST(wraparound_multiple_cycles);
    RUN_TEST(small_chunks_wraparound);

    printf("\nEdge Cases:\n");
    RUN_TEST(zero_length_push);
    RUN_TEST(zero_length_pop);
    RUN_TEST(alternating_push_pop);

    printf("\nData Integrity:\n");
    RUN_TEST(data_pattern_integrity);
    RUN_TEST(fifo_order_preserved);

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
