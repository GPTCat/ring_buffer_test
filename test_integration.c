/*
 * Integration tests for ring buffer
 * Tests multi-threaded SPSC correctness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "ring_buffer.c"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    fflush(stdout); \
    if (test_##name() == 0) { \
        printf(" PASS\n"); \
        tests_passed++; \
    } else { \
        printf(" FAIL\n"); \
        tests_failed++; \
    } \
} while(0)

/* ============ Helper to initialize buffer ============ */

static void init_buffer(ring_buffer_t *rb) {
    memset(rb, 0, sizeof(*rb));
    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
}

/* ============ Test Infrastructure ============ */

typedef struct {
    ring_buffer_t *rb;
    size_t num_messages;
    size_t message_size;
    atomic_bool *stop;
    atomic_size_t *produced;
    atomic_size_t *consumed;
} thread_args_t;

/* ============ Producer-Consumer Tests ============ */

static void *producer_fixed_size(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    uint8_t *data = malloc(args->message_size);

    for (size_t i = 0; i < args->num_messages; i++) {
        /* Fill with pattern */
        for (size_t j = 0; j < args->message_size; j++) {
            data[j] = (uint8_t)((i + j) & 0xFF);
        }

        /* Spin until push succeeds */
        while (!ring_push(args->rb, data, args->message_size)) {
            sched_yield();
        }
        atomic_fetch_add(args->produced, 1);
    }

    free(data);
    return NULL;
}

static void *consumer_fixed_size(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    uint8_t *data = malloc(args->message_size);

    for (size_t i = 0; i < args->num_messages; i++) {
        /* Spin until pop succeeds */
        while (!ring_pop(args->rb, data, args->message_size)) {
            sched_yield();
        }

        /* Verify pattern */
        for (size_t j = 0; j < args->message_size; j++) {
            if (data[j] != (uint8_t)((i + j) & 0xFF)) {
                fprintf(stderr, "Data corruption at msg %zu, byte %zu: expected %d, got %d\n",
                        i, j, (int)((i + j) & 0xFF), (int)data[j]);
                free(data);
                return (void *)1;
            }
        }
        atomic_fetch_add(args->consumed, 1);
    }

    free(data);
    return NULL;
}

TEST(spsc_small_messages) {
    ring_buffer_t rb;
    init_buffer(&rb);

    atomic_size_t produced = 0;
    atomic_size_t consumed = 0;
    atomic_bool stop = false;

    thread_args_t args = {
        .rb = &rb,
        .num_messages = 100000,
        .message_size = 8,
        .stop = &stop,
        .produced = &produced,
        .consumed = &consumed
    };

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, producer_fixed_size, &args);
    pthread_create(&consumer, NULL, consumer_fixed_size, &args);

    void *producer_result, *consumer_result;
    pthread_join(producer, &producer_result);
    pthread_join(consumer, &consumer_result);

    if (consumer_result != NULL) return 1;
    if (atomic_load(&produced) != args.num_messages) return 1;
    if (atomic_load(&consumed) != args.num_messages) return 1;

    return 0;
}

TEST(spsc_large_messages) {
    ring_buffer_t rb;
    init_buffer(&rb);

    atomic_size_t produced = 0;
    atomic_size_t consumed = 0;
    atomic_bool stop = false;

    thread_args_t args = {
        .rb = &rb,
        .num_messages = 10000,
        .message_size = 500,
        .stop = &stop,
        .produced = &produced,
        .consumed = &consumed
    };

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, producer_fixed_size, &args);
    pthread_create(&consumer, NULL, consumer_fixed_size, &args);

    void *producer_result, *consumer_result;
    pthread_join(producer, &producer_result);
    pthread_join(consumer, &consumer_result);

    if (consumer_result != NULL) return 1;
    if (atomic_load(&produced) != args.num_messages) return 1;
    if (atomic_load(&consumed) != args.num_messages) return 1;

    return 0;
}

TEST(spsc_single_byte_messages) {
    ring_buffer_t rb;
    init_buffer(&rb);

    atomic_size_t produced = 0;
    atomic_size_t consumed = 0;
    atomic_bool stop = false;

    thread_args_t args = {
        .rb = &rb,
        .num_messages = 1000000,
        .message_size = 1,
        .stop = &stop,
        .produced = &produced,
        .consumed = &consumed
    };

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, producer_fixed_size, &args);
    pthread_create(&consumer, NULL, consumer_fixed_size, &args);

    void *producer_result, *consumer_result;
    pthread_join(producer, &producer_result);
    pthread_join(consumer, &consumer_result);

    if (consumer_result != NULL) return 1;
    if (atomic_load(&produced) != args.num_messages) return 1;
    if (atomic_load(&consumed) != args.num_messages) return 1;

    return 0;
}

/* ============ Variable Size Messages ============ */

static void *producer_variable_size(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    uint8_t data[64];
    size_t msg_id = 0;

    while (msg_id < args->num_messages) {
        /* Size varies from 4-64 bytes (need at least 3 for header + 1 for data) */
        size_t size = (msg_id % 61) + 4;

        data[0] = (uint8_t)(msg_id & 0xFF);
        data[1] = (uint8_t)((msg_id >> 8) & 0xFF);
        data[2] = (uint8_t)size;

        for (size_t j = 3; j < size; j++) {
            data[j] = (uint8_t)((msg_id + j) & 0xFF);
        }

        if (ring_push(args->rb, data, size)) {
            atomic_fetch_add(args->produced, 1);
            msg_id++;
        } else {
            sched_yield();
        }
    }

    return NULL;
}

static void *consumer_variable_size(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    uint8_t data[64];
    size_t expected_id = 0;

    while (expected_id < args->num_messages) {
        size_t expected_size = (expected_id % 61) + 4;

        if (ring_pop(args->rb, data, expected_size)) {
            size_t got_id = data[0] | ((size_t)data[1] << 8);
            if (got_id != expected_id) {
                fprintf(stderr, "Message ordering error: expected %zu, got %zu\n",
                        expected_id, got_id);
                return (void *)1;
            }

            if (data[2] != (uint8_t)expected_size) {
                fprintf(stderr, "Size mismatch at msg %zu\n", expected_id);
                return (void *)1;
            }

            atomic_fetch_add(args->consumed, 1);
            expected_id++;
        } else {
            sched_yield();
        }
    }

    return NULL;
}

TEST(spsc_variable_size_messages) {
    ring_buffer_t rb;
    init_buffer(&rb);

    atomic_size_t produced = 0;
    atomic_size_t consumed = 0;
    atomic_bool stop = false;

    thread_args_t args = {
        .rb = &rb,
        .num_messages = 50000,
        .message_size = 0,
        .stop = &stop,
        .produced = &produced,
        .consumed = &consumed
    };

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, producer_variable_size, &args);
    pthread_create(&consumer, NULL, consumer_variable_size, &args);

    void *producer_result, *consumer_result;
    pthread_join(producer, &producer_result);
    pthread_join(consumer, &consumer_result);

    if (consumer_result != NULL) return 1;
    if (atomic_load(&produced) != args.num_messages) return 1;
    if (atomic_load(&consumed) != args.num_messages) return 1;

    return 0;
}

/* ============ Burst Test ============ */

typedef struct {
    ring_buffer_t *rb;
    size_t burst_size;
    size_t num_bursts;
    atomic_size_t *produced;
} burst_prod_args_t;

typedef struct {
    ring_buffer_t *rb;
    size_t total_msgs;
    atomic_size_t *consumed;
    int *error;
} burst_cons_args_t;

static void *burst_producer(void *arg) {
    burst_prod_args_t *a = (burst_prod_args_t *)arg;
    uint8_t data[8];

    for (size_t burst = 0; burst < a->num_bursts; burst++) {
        for (size_t i = 0; i < a->burst_size; i++) {
            size_t msg_id = burst * a->burst_size + i;
            memcpy(data, &msg_id, sizeof(msg_id));

            while (!ring_push(a->rb, data, 8)) {
                sched_yield();
            }
            atomic_fetch_add(a->produced, 1);
        }
        usleep(100);
    }
    return NULL;
}

static void *burst_consumer(void *arg) {
    burst_cons_args_t *a = (burst_cons_args_t *)arg;
    uint8_t data[8];
    size_t expected = 0;

    while (expected < a->total_msgs) {
        if (ring_pop(a->rb, data, 8)) {
            size_t got;
            memcpy(&got, data, sizeof(got));
            if (got != expected) {
                fprintf(stderr, "Burst order error: expected %zu, got %zu\n",
                        expected, got);
                *a->error = 1;
                return NULL;
            }
            atomic_fetch_add(a->consumed, 1);
            expected++;
        } else {
            sched_yield();
        }
    }
    return NULL;
}

TEST(spsc_burst_pattern) {
    ring_buffer_t rb;
    init_buffer(&rb);

    const size_t burst_size = 100;
    const size_t num_bursts = 1000;

    atomic_size_t produced = 0;
    atomic_size_t consumed = 0;
    int error = 0;

    burst_prod_args_t prod_args = {
        .rb = &rb,
        .burst_size = burst_size,
        .num_bursts = num_bursts,
        .produced = &produced
    };

    burst_cons_args_t cons_args = {
        .rb = &rb,
        .total_msgs = burst_size * num_bursts,
        .consumed = &consumed,
        .error = &error
    };

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, burst_producer, &prod_args);
    pthread_create(&consumer, NULL, burst_consumer, &cons_args);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    if (error) return 1;
    if (atomic_load(&produced) != burst_size * num_bursts) return 1;
    if (atomic_load(&consumed) != burst_size * num_bursts) return 1;

    return 0;
}

int main(void) {
    printf("Running integration tests...\n\n");

    printf("SPSC Correctness:\n");
    RUN_TEST(spsc_small_messages);
    RUN_TEST(spsc_large_messages);
    RUN_TEST(spsc_single_byte_messages);

    printf("\nStress Tests:\n");
    RUN_TEST(spsc_variable_size_messages);
    RUN_TEST(spsc_burst_pattern);

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
