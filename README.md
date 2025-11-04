# my_alloc-v2
#OS

my_alloc

A low-level, alignment-aware, thread-safe, and segregated free list–based dynamic memory allocator that implements `malloc()`, `calloc()`, `realloc()`, and `free()` from scratch—built directly on top of `sbrk()`.

---

## Overview

**my_alloc** is a fully custom memory allocator that emulates the behavior of the standard C library’s `malloc` family, designed for systems and research use.  
It offers:

- **Manual heap management** (via `sbrk()`)
- **Custom metadata tracking** for every allocated block
- **Segregated free lists** organized by size class
- **Thread-safe allocation** with per-class fine-grained mutexes
- **Configurable alignment** up to 16 bytes

Originally a learning project, **my_alloc** now provides an allocator core akin to the philosophies behind minimalist allocators like early `dlmalloc` or `jemalloc`.

---

**Allocation Features**
- **Alignment & Rounding:** Rounds requests up to nearest multiple of 8 bytes
- **Segregated Free Lists** 
- **Fragmentation Reduction:** Splits large blocks, coalesces adjacent free blocks
- **Thread Safety**
- **Efficient Reuse:** Block splitting/coalescing

---

## Quick Start

compile and test run:

```bash
gcc my_alloc.c -o my_alloc

./my_alloc
```

## How it works

Each memory block has metadata stored right before the user pointer:

```
Heap: [metadata][user data][metadata][user data]...
               ↑                     ↑
          returned ptr         returned ptr
```

## API

### Function Signatures
```C
void* my_alloc(size_t size); // malloc()
void freee(void* ptr); // free()
void* call_oc(size_t nelem, size_t elsize); // calloc()
void* reall_oc(void* ptr, size_t size); // realloc()
```


All functions mirror their libc equivalents in both naming and semantics.

---

## ⚙️ Configuration

| Setting      | Default | Description                          |
|--------------|---------|--------------------------------------|
| ALIGNMENT    | 8       | Byte alignment boundary              |
| NUM_CLASSES  | 10      | Segregated size class count          |
| THREAD_SAFE  | Enabled | Per-class mutexes                    |

Modify in `my_alloc.h` or compile-time defines.

---

## Limitations

- Does *not* reclaim memory to the OS (e.g., `brk`, `munmap`)
- No advanced best-fit/size hinting heuristics
- Metadata/debug overhead visible in debug mode

---
