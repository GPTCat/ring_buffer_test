CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE
CFLAGS_OPT = $(CFLAGS) -O2
CFLAGS_DEBUG = $(CFLAGS) -g -fsanitize=address,undefined
LDFLAGS = -pthread

.PHONY: all clean test test-unit test-integration test-bench

all: test_unit test_integration test_bench

# Unit tests
test_unit: test_unit.c ring_buffer.c
	$(CC) $(CFLAGS_DEBUG) -o $@ $< $(LDFLAGS)

# Integration tests (multi-threaded)
test_integration: test_integration.c ring_buffer.c
	$(CC) $(CFLAGS_DEBUG) -o $@ $< $(LDFLAGS)

# Benchmark (optimized build)
test_bench: test_bench.c ring_buffer.c
	$(CC) $(CFLAGS_OPT) -o $@ $< $(LDFLAGS)

# Run all tests
test: test-unit test-integration

test-unit: test_unit
	./test_unit

test-integration: test_integration
	./test_integration

test-bench: test_bench
	./test_bench

clean:
	rm -f test_unit test_integration test_bench
