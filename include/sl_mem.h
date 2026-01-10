#ifndef SL_MEM_H_
#define SL_MEM_H_

#include <stddef.h>
#include <stdbool.h>

#ifndef SL_DEBUG

// Allocate a new chunck of memory.
// On failure the function returns NULL.
void *memAlloc(size_t objectCount, size_t objectSize);
// Allocate a new chunck of memory given the size in bytes.
// On failure the function returns NULL.
void *memAllocBytes(size_t byteCount);
// Allocate a new chunck of memory that is zeroed.
// On failure the function returns NULL.
void *memAllocZeroed(size_t objectCount, size_t objectSize);
// Allocate a new chunck of memory that is zeroed.
// On failure the function returns NULL.
void *memAllocZeroedBytes(size_t byteCount);

// Increase the size of a memory block.
// On failure the program is aborted.
void *memExpand(void *block, size_t newObjectCount, size_t objectSize);
// Increase the size of a memory block given the new size in bytes.
// On failure the program is aborted.
void *memExpandBytes(void *block, size_t newByteCount);

// Decrease the size of a memory block.
// If the block cannot be shrunk the block itself is returned.
void *memShrink(void *block, size_t newObjectCount, size_t objectSize);
// Decrease the size of a memory block given the new size in bytes.
// If the block cannot be shrunk the block itself is returned.
void *memShrinkBytes(void *block, size_t newByteCount);

// Change the state of `block` depending on `objectCount`.
// If `block == NULL` new memory will be allocated.
// If `block != NULL` and `objectCount == 0` the block will be free'd.
// Otherwise the block is reallocated.
// On failure the program is aborted.
void *memChange(void *block, size_t objectCount, size_t objectSize);

// Change the state of `block` depending on `byteCount`.
// If `block == NULL` new memory will be allocated.
// If `block != NULL` and `byteCount == 0` the block will be free'd.
// Otherwise the block is reallocated.
// On failure the program is aborted.
void *memChangeBytes(void *block, size_t byteCount);

// Free a block of memory. Do nothing if `block == NULL`
void memFree(void *block);

// Debug-mode only
#define memInit() true
// Debug-mode only
#define memQuit()
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

bool memInit(void);
void memQuit(void);

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

// Check if there are any allocations
bool memHasAllocs(void);
// Print all allocations
void memPrintAllocs(void);
// Free all allocations
void memFreeAllAllocs(void);
// Check wether an out-of-bounds write happend to a block
#define memCheckBounds(block) _memCheckBounds(block, __LINE__, __FILE__)
void _memCheckBounds(void *block, uint32_t line, const char *file);
// Chcek if a pointer points to a heap-allocated memory block
bool memIsAlloc(void *block);

#endif // !NV_DEBUG

#endif // !SL_MEM_H_
