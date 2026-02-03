/*
 * Performance benchmarks for ring buffer
 * Measures throughput and latency in SPSC scenarios
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#include "ring_buffer.c"

/* ============ Helper ============ */

static void init_buffer(ring_buffer_t *rb) {
    memset(rb, 0, sizeof(*rb));
    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
}

/* ============ Timing Utilities ============ */

static inline uint64_t get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ============ Throughput Benchmark ============ */

typedef struct {
    ring_buffer_t *rb;
    size_t num_messages;
    size_t message_size;
    atomic_bool *done;
} bench_args_t;

static void *throughput_producer(void *arg) {
    bench_args_t *args = (bench_args_t *)arg;
    uint8_t *data = calloc(1, args->message_size);

    for (size_t i = 0; i < args->num_messages; i++) {
        while (!ring_push(args->rb, data, args->message_size)) {
            /* Spin */
        }
    }

    free(data);
    return NULL;
}

static void *throughput_consumer(void *arg) {
    bench_args_t *args = (bench_args_t *)arg;
    uint8_t *data = calloc(1, args->message_size);

    for (size_t i = 0; i < args->num_messages; i++) {
        while (!ring_pop(args->rb, data, args->message_size)) {
            /* Spin */
        }
    }

    free(data);
    atomic_store(args->done, true);
    return NULL;
}

static void bench_throughput(size_t message_size, size_t num_messages) {
    ring_buffer_t rb;
    init_buffer(&rb);

    atomic_bool done = false;

    bench_args_t args = {
        .rb = &rb,
        .num_messages = num_messages,
        .message_size = message_size,
        .done = &done
    };

    pthread_t producer, consumer;

    uint64_t start = get_nanos();

    pthread_create(&producer, NULL, throughput_producer, &args);
    pthread_create(&consumer, NULL, throughput_consumer, &args);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    uint64_t end = get_nanos();
    uint64_t elapsed_ns = end - start;

    double elapsed_sec = (double)elapsed_ns / 1e9;
    double msgs_per_sec = (double)num_messages / elapsed_sec;
    double bytes_per_sec = msgs_per_sec * (double)message_size;
    double ns_per_msg = (double)elapsed_ns / (double)num_messages;

    printf("  %3zu bytes x %8zu msgs: %10.2f msg/s  %7.2f MB/s  %6.1f ns/msg\n",
           message_size, num_messages, msgs_per_sec,
           bytes_per_sec / (1024.0 * 1024.0), ns_per_msg);
}

/* ============ Latency Benchmark ============ */

typedef struct {
    ring_buffer_t *rb;
    size_t num_samples;
    size_t message_size;
    uint64_t *send_times;
    atomic_bool *ready;
} latency_prod_args_t;

typedef struct {
    ring_buffer_t *rb;
    size_t num_samples;
    size_t message_size;
    uint64_t *recv_times;
    atomic_bool *ready;
} latency_cons_args_t;

static void *latency_producer(void *arg) {
    latency_prod_args_t *args = (latency_prod_args_t *)arg;
    uint8_t *data = calloc(1, args->message_size);

    while (!atomic_load(args->ready)) {
        sched_yield();
    }

    for (size_t i = 0; i < args->num_samples; i++) {
        args->send_times[i] = get_nanos();
        memcpy(data, &i, sizeof(i));

        while (!ring_push(args->rb, data, args->message_size)) {
            /* Spin */
        }

        /* Small delay to avoid overwhelming */
        for (volatile int j = 0; j < 100; j++);
    }

    free(data);
    return NULL;
}

static void *latency_consumer(void *arg) {
    latency_cons_args_t *args = (latency_cons_args_t *)arg;
    uint8_t *data = calloc(1, args->message_size);

    atomic_store(args->ready, true);

    for (size_t i = 0; i < args->num_samples; i++) {
        while (!ring_pop(args->rb, data, args->message_size)) {
            /* Spin */
        }
        args->recv_times[i] = get_nanos();
    }

    free(data);
    return NULL;
}

static int compare_uint64(const void *a, const void *b) {
    uint64_t ua = *(const uint64_t *)a;
    uint64_t ub = *(const uint64_t *)b;
    return (ua > ub) - (ua < ub);
}

static void bench_latency(size_t message_size, size_t num_samples) {
    ring_buffer_t rb;
    init_buffer(&rb);

    uint64_t *send_times = malloc(num_samples * sizeof(uint64_t));
    uint64_t *recv_times = malloc(num_samples * sizeof(uint64_t));
    uint64_t *latencies = malloc(num_samples * sizeof(uint64_t));
    atomic_bool ready = false;

    latency_prod_args_t prod_args = {
        .rb = &rb,
        .num_samples = num_samples,
        .message_size = message_size,
        .send_times = send_times,
        .ready = &ready
    };

    latency_cons_args_t cons_args = {
        .rb = &rb,
        .num_samples = num_samples,
        .message_size = message_size,
        .recv_times = recv_times,
        .ready = &ready
    };

    pthread_t producer, consumer;

    pthread_create(&consumer, NULL, latency_consumer, &cons_args);
    pthread_create(&producer, NULL, latency_producer, &prod_args);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    for (size_t i = 0; i < num_samples; i++) {
        latencies[i] = recv_times[i] - send_times[i];
    }

    qsort(latencies, num_samples, sizeof(uint64_t), compare_uint64);

    uint64_t min = latencies[0];
    uint64_t p50 = latencies[num_samples / 2];
    uint64_t p90 = latencies[(num_samples * 90) / 100];
    uint64_t p99 = latencies[(num_samples * 99) / 100];
    uint64_t p999 = latencies[(num_samples * 999) / 1000];
    uint64_t max = latencies[num_samples - 1];

    uint64_t sum = 0;
    for (size_t i = 0; i < num_samples; i++) {
        sum += latencies[i];
    }
    double mean = (double)sum / (double)num_samples;

    printf("  %3zu bytes (%zu samples):\n", message_size, num_samples);
    printf("    min: %5lu ns  p50: %5lu ns  p90: %5lu ns  p99: %5lu ns  p99.9: %5lu ns  max: %6lu ns  mean: %.1f ns\n",
           min, p50, p90, p99, p999, max, mean);

    free(send_times);
    free(recv_times);
    free(latencies);
}

/* ============ Contention Benchmark ============ */

typedef struct {
    ring_buffer_t *rb;
    size_t num_ops;
    size_t msg_size;
    atomic_size_t *fails;
} contention_args_t;

static void *contention_producer(void *arg) {
    contention_args_t *a = (contention_args_t *)arg;
    uint8_t data[8] = {0};
    for (size_t i = 0; i < a->num_ops; i++) {
        while (!ring_push(a->rb, data, a->msg_size)) {
            atomic_fetch_add(a->fails, 1);
        }
    }
    return NULL;
}

static void *contention_consumer(void *arg) {
    contention_args_t *a = (contention_args_t *)arg;
    uint8_t data[8];
    for (size_t i = 0; i < a->num_ops; i++) {
        while (!ring_pop(a->rb, data, a->msg_size)) {
            atomic_fetch_add(a->fails, 1);
        }
    }
    return NULL;
}

static void bench_contention(void) {
    ring_buffer_t rb;
    init_buffer(&rb);

    const size_t num_ops = 10000000;
    const size_t msg_size = 8;

    atomic_size_t push_fails = 0;
    atomic_size_t pop_fails = 0;

    contention_args_t prod_args = { &rb, num_ops, msg_size, &push_fails };
    contention_args_t cons_args = { &rb, num_ops, msg_size, &pop_fails };

    pthread_t producer, consumer;

    uint64_t start = get_nanos();

    pthread_create(&producer, NULL, contention_producer, &prod_args);
    pthread_create(&consumer, NULL, contention_consumer, &cons_args);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    uint64_t end = get_nanos();

    printf("  %zu ops, %zu byte messages:\n", num_ops, msg_size);
    printf("    Total time: %.3f ms\n", (double)(end - start) / 1e6);
    printf("    Push retries: %zu (%.4f%%)\n",
           atomic_load(&push_fails),
           100.0 * (double)atomic_load(&push_fails) / (double)num_ops);
    printf("    Pop retries: %zu (%.4f%%)\n",
           atomic_load(&pop_fails),
           100.0 * (double)atomic_load(&pop_fails) / (double)num_ops);
}

/* ============ Single-threaded Baseline ============ */

static void bench_single_threaded(void) {
    ring_buffer_t rb;
    init_buffer(&rb);

    const size_t num_ops = 10000000;
    uint8_t data[8] = {0};
    uint8_t out[8];

    uint64_t start = get_nanos();

    for (size_t i = 0; i < num_ops; i++) {
        ring_push(&rb, data, 8);
        ring_pop(&rb, out, 8);
    }

    uint64_t end = get_nanos();

    double ns_per_pair = (double)(end - start) / (double)num_ops;
    double ops_per_sec = 1e9 / ns_per_pair * 2;

    printf("  %zu push+pop pairs:\n", num_ops);
    printf("    Total time: %.3f ms\n", (double)(end - start) / 1e6);
    printf("    %.1f ns per push+pop pair\n", ns_per_pair);
    printf("    %.2f M ops/sec\n", ops_per_sec / 1e6);
}

/* ============ Main ============ */

int main(void) {
    printf("Ring Buffer Performance Benchmarks\n");
    printf("===================================\n\n");

    printf("Single-threaded baseline:\n");
    bench_single_threaded();

    printf("\nThroughput (SPSC, spinning):\n");
    bench_throughput(1, 10000000);
    bench_throughput(8, 10000000);
    bench_throughput(64, 5000000);
    bench_throughput(256, 2000000);
    bench_throughput(512, 1000000);

    printf("\nLatency distribution (SPSC):\n");
    bench_latency(8, 100000);
    bench_latency(64, 100000);
    bench_latency(256, 50000);

    printf("\nContention analysis:\n");
    bench_contention();

    printf("\n===================================\n");
    printf("Benchmark complete.\n");

    return 0;
}
