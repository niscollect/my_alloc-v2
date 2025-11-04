# my_alloc-v2
#OS

my_alloc

A low-level, alignment-aware, thread-safe, and segregated free list–based dynamic memory allocator implementing malloc(), calloc(), realloc(), and free() from scratch — built directly on top of sbrk().

What is this?

my_alloc is a full-fledged custom memory allocator that emulates the behavior of the standard C library’s malloc family.
It provides:

Manual heap management using sbrk()

Custom metadata tracking for each block

Free list organization by size class (segregated lists)

Thread-safe allocation through fine-grained locking

Alignment-aware allocations up to 16 bytes (configurable)

This project started as a learning experiment and evolved into a working allocator core similar in spirit to minimalist allocators like dlmalloc and jemalloc’s early designs.

Core Design

Each allocated block is prefixed by metadata:

+----------------+--------------------+
|  block_header  |  user memory ...   |
+----------------+--------------------+
                  ↑
            returned pointer


Every block maintains:

size — total payload size

is_free — flag indicating block status

next — pointer to the next block in the same free list

Allocation flow

Alignment & size rounding

Rounds up requests to the nearest multiple of 8 or 16 bytes.

Segregated free lists

Maintains multiple free lists for different size ranges (e.g., 16B, 32B, 64B, etc.).

Reduces fragmentation and lookup time.

Thread safety

Uses a pthread_mutex_t per size class.

Avoids global locking.

Block splitting

Splits oversized free blocks for better reuse.

Coalescing

Adjacent free blocks are merged to mitigate fragmentation.

Quick Start
gcc my_alloc.c -pthread -o my_alloc
./my_alloc


API
void* my_alloc(size_t size);
void  freee(void* ptr);
void* call_oc(size_t nelem, size_t elsize);
void* reall_oc(void* ptr, size_t size);


All functions mimic their libc counterparts in behavior and naming:

my_alloc() → malloc()

freee() → free()

call_oc() → calloc()

reall_oc() → realloc()

Configuration
Setting	Default	Description
ALIGNMENT	8	Byte alignment boundary
NUM_CLASSES	10	Number of segregated size classes
THREAD_SAFE	Enabled	Enables per-class mutexes

Limitations

No memory reclamation to the OS (via brk or munmap)

Lacks advanced heuristics (e.g., best-fit, size hinting)

Debugging mode may show extra metadata overhead
