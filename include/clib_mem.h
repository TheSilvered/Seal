/*
Memory allocation library.

License: MIT license, see the bottom of the file.

Macros:
- define `CLIB_MEM_STDLIB_FUNCS` to add macros that replace standard `malloc`,
  `calloc`, `realloc` and `free` with their equivalent in the library.
- define `CLIB_MEM_TRACE_ALLOCS` to trace all allocations. In this mode the
  library depends by default on `clib_threads` to be thread-safe but this can be
  disabled.
- define `CLIB_MEM_NO_THREADS` to remove dependency on `clib_threads`. However
  the library is no longer thread-safe.
- define `CLIB_MEM_ABORT_ON_FAIL` to log "Out of memory." and abort the program
  if a memory allocation fails.

Function macros:
- define `memFail(...)` to change the behaviour when a memory allocation fails.
  The arguments are passed as if it were `printf`.
  By default nothing happens and `NULL` is returned.
  When `CLIB_MEM_ABORT_ON_FAIL` is defined an internal definition is used to
  abort the program. Any user-defined `memFail` always overrides the internal
  definition.
- define `memLog(...)` to change the logging function.
  The arguments are passed as if it were `printf`.
  By default it uses `fprintf` and prints to `stderr`.
- define `memAssert` to change assertions. By default it is the standard
  `assert`.
*/

#ifndef CLIB_MEM_H_
#define CLIB_MEM_H_

#define CLIB_MEM_NO_THREADS

#include <stddef.h>
#include <stdbool.h>

#ifdef CLIB_MEM_STDLIB_FUNCS
#define malloc memAllocBytes
#define calloc memAllocZeroed
#define realloc memChangeBytes
#define free memFree
#endif // !CLIB_MEM_STDLIB_FUNCS

#ifndef CLIB_MEM_TRACE_ALLOCS

// Allocate a new chunk of memory.
void *memAlloc(size_t objectCount, size_t objectSize);
// Allocate a new chunk of memory given the size in bytes.
void *memAllocBytes(size_t byteCount);
// Allocate a new chunk of memory that is zeroed.
void *memAllocZeroed(size_t objectCount, size_t objectSize);
// Allocate a new chunk of memory that is zeroed.
void *memAllocZeroedBytes(size_t byteCount);

// Increase the size of a memory block.
void *memExpand(void *block, size_t newObjectCount, size_t objectSize);
// Increase the size of a memory block given the new size in bytes.
void *memExpandBytes(void *block, size_t newByteCount);

// Decrease the size of a memory block.
// If the block cannot be shrunk the block itself is returned.
// Shrinking to a size of 0 is equivalent to freeing the block.
void *memShrink(void *block, size_t newObjectCount, size_t objectSize);
// Decrease the size of a memory block given the new size in bytes.
// If the block cannot be shrunk the block itself is returned.
// Shrinking to a size of 0 is equivalent to freeing the block.
void *memShrinkBytes(void *block, size_t newByteCount);

// Change the state of `block` depending on `objectCount`.
// If `block == NULL` new memory will be allocated.
// If `block != NULL` and `objectCount == 0` the block will be freed.
// Otherwise the block is reallocated.
void *memChange(void *block, size_t objectCount, size_t objectSize);

// Change the state of `block` depending on `byteCount`.
// If `block == NULL` new memory will be allocated.
// If `block != NULL` and `byteCount == 0` the block will be freed.
// Otherwise the block is reallocated.
void *memChangeBytes(void *block, size_t byteCount);

// Free a block of memory. Do nothing if `block == NULL`
void memFree(void *block);

// Debug-mode only
#define memHasAllocs() false
// Debug-mode only
#define memPrintAllocs()
// Debug-mode only
#define memFreeAllAllocs()
// Debug-mode only
#define memCheckBounds(...)
// Debug-mode only
#define memIsAlloc(...) true

#else

#define memAlloc(objectCount, objectSize)                                      \
    _memAlloc(objectCount, objectSize, __LINE__, __FILE__)

#define memAllocBytes(byteCount)                                               \
    _memAllocBytes(byteCount, __LINE__, __FILE__)

#define memAllocZeroed(objectCount, objectSize)                                \
    _memAllocZeroed(objectCount, objectSize, __LINE__, __FILE__)

#define memAllocZeroedBytes(byteCount)                                         \
    _memAllocZeroedBytes(byteCount, __LINE__, __FILE__)

#define memExpand(block, newObjectCount, objectSize)                           \
    _memExpand(block, newObjectCount, objectSize, __LINE__, __FILE__)

#define memExpandBytes(block, newByteCount)                                    \
    _memExpandBytes(block, newByteCount, __LINE__, __FILE__)

#define memShrink(block, newObjectCount, objectSize)                           \
    _memShrink(block, newObjectCount, objectSize, __LINE__, __FILE__)

#define memShrinkBytes(block, newByteCount)                                    \
    _memShrinkBytes(block, newByteCount, __LINE__, __FILE__)

#define memChange(block, objectCount, objectSize)                              \
    _memChange(block, objectCount, objectSize, __LINE__, __FILE__)

#define memChangeBytes(block, byteCount)                                       \
    _memChangeBytes(block, byteCount, __LINE__, __FILE__)

#define memFree(block)                                                         \
    _memFree(block, __LINE__, __FILE__)

// Internal function for memory tracking

#include <stdint.h>

void *_memAlloc(
    size_t objectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
);
void *_memAllocBytes(size_t byteCount, uint32_t line, const char *file);
void *_memAllocZeroed(
    size_t objectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
);
void *_memAllocZeroedBytes(size_t byteCount, uint32_t line, const char *file);
void *_memExpand(
    void *block,
    size_t newObjectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
);
void *_memExpandBytes(
    void *block,
    size_t newByteCount,
    uint32_t line,
    const char *file
);
void *_memShrink(
    void *block,
    size_t newObjectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
);
void *_memShrinkBytes(
    void *block,
    size_t newByteCount,
    uint32_t line,
    const char *file
);
void *_memChange(
    void *block,
    size_t objectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
);
void *_memChangeBytes(
    void *block,
    size_t byteCount,
    uint32_t line,
    const char *file
);
void _memFree(void *block, uint32_t line, const char *file);

// Check if there are any allocations.
bool memHasAllocs(void);
// Print all allocations.
void memPrintAllocs(void);
// Free all allocations.
void memFreeAllAllocs(void);
// Check whether an out-of-bounds write happened to a block.
#define memCheckBounds(block) _memCheckBounds(block, __LINE__, __FILE__)
void _memCheckBounds(void *block, uint32_t line, const char *file);
// Check if a pointer points to a heap-allocated memory block.
bool memIsAlloc(void *block);

#endif // !CLIB_MEM_TRACE_ALLOCS

#endif // !CLIB_MEM_H_

/*
MIT License

Copyright (c) 2026 Davide Taffarello

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
