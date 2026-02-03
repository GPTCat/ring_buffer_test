# Zero-Copy Ring Buffer

A minimal, lock-free ring buffer implementation in C for high-frequency systems. Single-producer, single-consumer (SPSC) design optimized for performance.

## Why This Exists

The producer-consumer problem is everywhere. One thread generates data, another thread processes it. The naive approach—slap a mutex around a queue—works, but mutexes are expensive: context switches, kernel involvement, cache coherency traffic. It all adds up when you're trying to push millions of messages per second.

Then there's the copying problem. If you `malloc()` for every message and `memcpy()` data around, you're burning CPU cycles. Zero-copy means writing data once, in place, and letting the consumer read it directly from the same spot.

## Features

- **Lock-free**: No mutexes, no syscalls, no scheduler involvement
- **Zero-copy**: Write directly into the buffer, read directly from it
- **Cache-optimized**: Head and tail pointers are cache-line aligned to prevent false sharing
- **Minimal**: ~50 lines of C, no dependencies beyond C11 standard library

## Building

```bash
make all        # Build everything
make test       # Run unit and integration tests
make test-bench # Run performance benchmarks
```

Or compile directly:

```bash
gcc -std=c11 -O2 -c ring_buffer.c -o ring_buffer.o
```

## Usage

```c
#include "ring_buffer.c"

ring_buffer_t rb = {0};  // Zero-initialize

// Producer thread
uint8_t data[] = {1, 2, 3, 4};
if (ring_push(&rb, data, sizeof(data))) {
    // Success
}

// Consumer thread
uint8_t buf[4];
if (ring_pop(&rb, buf, sizeof(buf))) {
    // Success, data is in buf
}
```

## API

| Function | Description |
|----------|-------------|
| `ring_push(rb, src, len)` | Write `len` bytes from `src` into buffer. Returns `false` if insufficient space. |
| `ring_pop(rb, dst, len)` | Read `len` bytes from buffer into `dst`. Returns `false` if insufficient data. |

## Memory Ordering

This is the part everyone messes up. The implementation uses:

- `memory_order_relaxed` for reading your own index (you're the only writer)
- `memory_order_acquire` for reading the other thread's index (synchronize with their release)
- `memory_order_release` for storing your own index (publish your updates)

When the producer does `atomic_store(&head, new_head, memory_order_release)`, it guarantees all previous writes (the actual data) are visible before the head update. When the consumer does `atomic_load(&head, memory_order_acquire)`, it sees those writes.

## Limitations

- **SPSC only**: Single producer, single consumer. Multiple producers would need CAS loops.
- **Fixed size**: 1024 bytes, compile-time constant. Power-of-2 enables fast modulo via bitwise AND.
- **Usable capacity**: `BUFFER_SIZE - 1` (one slot reserved to distinguish full from empty)

## Performance

On modest hardware, this pattern can push millions of tiny messages per second. Trading systems use this for market data. Database WAL implementations use it for log writes. Kernels use it for device I/O buffers.

Run `make test-bench` to see numbers on your machine.

## License

Public domain / MIT / do whatever you want.
