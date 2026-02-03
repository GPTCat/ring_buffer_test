# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A minimal, lock-free ring buffer implementation in C. Single-producer, single-consumer (SPSC) design optimized for performance.

## Building

No build system is configured. This is a single-file implementation meant to be included in other projects. Compile with any C11-compatible compiler:

```bash
gcc -std=c11 -O2 -c ring_buffer.c -o ring_buffer.o
clang -std=c11 -O2 -c ring_buffer.c -o ring_buffer.o
```

To use in a project, either include the source directly or link the object file.

## Architecture

**Core data structure** (`ring_buffer_t`):
- Fixed 1024-byte circular buffer
- Atomic head/tail pointers with cache-line alignment (64 bytes) to prevent false sharing
- Power-of-2 sizing enables bitwise AND for modulo operations

**Memory ordering strategy**:
- `memory_order_relaxed` for reading the local pointer (head in push, tail in pop)
- `memory_order_acquire` for reading the remote pointer (synchronizes with the other thread's release)
- `memory_order_release` for updating the local pointer (publishes the data written/consumed)

**API**:
- `ring_push(rb, src, len)` - Write data to buffer, returns false if insufficient space
- `ring_pop(rb, dst, len)` - Read data from buffer, returns false if insufficient data

## Key Constraints

- Buffer size is compile-time fixed (1024 bytes)
- Data type is `uint8_t` (byte-oriented)
- No dynamic allocation
- SPSC only (one producer thread, one consumer thread)
- Maximum usable capacity is BUFFER_SIZE - 1 (one slot reserved to distinguish full from empty)
