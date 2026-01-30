#include <stdlib.h>
#include "clib_mem.h"

#ifdef _MSC_VER
#pragma warning(disable : 4702) // unreachable code
#endif // !_MSC_VER

#ifndef memLog
#include <stdio.h>
#define memLog(...) fprintf(stderr, __VA_ARGS__)
#endif // !memLog

#ifndef memFail
#ifdef CLIB_MEM_ABORT_ON_FAIL
#define memFail(...)                                                           \
    do {                                                                       \
        memLog(__VA_ARGS__);                                                   \
        abort();                                                               \
    } while (0)
#else
#define memFail(...)
#endif // !CLIB_MEM_ABORT_ON_FAIL
#endif // !memFail

#ifndef memAssert
#include <assert.h>
#define memAssert assert
#endif // !memAssert

#ifdef CLIB_MEM_STDLIB_FUNCS
#undef malloc
#undef calloc
#undef realloc
#undef free
#endif // !CLIB_MEM_STDLIB_FUNCS

#ifndef CLIB_MEM_TRACE_ALLOCS

void *memAlloc(size_t objectSize, size_t objectCount) {
    const size_t size = objectSize * objectCount;
    // Detect overflow
    memAssert(size == 0 || size / objectCount == objectSize);
    void *block = malloc(objectSize * objectCount);
    if (block == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }
    return block;
}

void *memAllocBytes(size_t byteCount) {
    void *block = malloc(byteCount);
    if (block == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }
    return block;
}

void *memAllocZeroed(size_t objectCount, size_t objectSize) {
    void *block = calloc(objectCount, objectSize);
    if (block == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }
    return block;
}

void *memAllocZeroedBytes(size_t byteCount) {
    void *block = calloc(byteCount, 1);
    if (block == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }
    return block;
}

void *memExpand(void *block, size_t objectSize, size_t newCount) {
    const size_t size = objectSize * newCount;
    memAssert(size != 0);
    // Detect overflow
    memAssert(size / newCount == objectSize);
    void *newBlock;
    if (block == NULL) {
        newBlock = malloc(size);
    } else {
        newBlock = realloc(block, size);
    }
    if (newBlock == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }
    return newBlock;
}

void *memExpandBytes(void *block, size_t newByteCount) {
    memAssert(newByteCount != 0);
    void *newBlock;
    if (block == NULL) {
        newBlock = malloc(newByteCount);
    } else {
        newBlock = realloc(block, newByteCount);
    }
    if (newBlock == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }
    return newBlock;
}

void *memShrink(void *block, size_t objectSize, size_t newCount) {
    size_t newSize = objectSize * newCount;
    // Detect overflow
    memAssert(newSize == 0 || newSize / newCount == objectSize);
    if (newSize == 0) {
        memFree(block);
        return NULL;
    }
    memAssert(block != NULL);
    void *newBlock = realloc(block, newSize);
    if (newBlock == NULL) {
        return block;
    }
    return newBlock;
}

void *memShrinkBytes(void *block, size_t newByteCount) {
    if (newByteCount == 0) {
        memFree(block);
        return NULL;
    }
    memAssert(block != NULL);
    void *newBlock = realloc(block, newByteCount);
    if (newBlock == NULL) {
        return block;
    }
    return newBlock;
}

void *memChange(void *block, size_t objectSize, size_t objectCount) {
    if (block == NULL) {
        return memAlloc(objectSize, objectCount);
    } else if (objectSize == 0 || objectCount == 0) {
        memFree(block);
        return NULL;
    } else {
        const size_t size = objectSize * objectCount;
        // Detect overflow
        memAssert(size == 0 || size / objectCount == objectSize);
        void *newBlock = realloc(block, size);
        if (newBlock == NULL) {
            memFail("Out of memory.\n");
            return NULL;
        }
        return newBlock;
    }
}

void *memChangeBytes(void *block, size_t byteCount) {
    if (block == NULL) {
        return memAllocBytes(byteCount);
    } else if (byteCount == 0) {
        memFree(block);
        return NULL;
    } else {
        void *newBlock = realloc(block, byteCount);
        if (newBlock == NULL) {
            memFail("Out of memory.\n");
            return NULL;
        }
        return newBlock;
    }
}

void memFree(void *block) {
    if (block != NULL) {
        free(block);
    }
}

#else

#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#ifdef CLIB_MEM_NO_THREADS
#define threadMutexLock(...) true
#define threadMutexUnlock(...) true
#else
#include "clib_threads.h"
static ThreadMutex g_memMutex = ThreadMutexInitializer;
#endif // !CLIB_MEM_NO_THREADS

#define _sentinelLen 4
#define _garbageByte 0xcd

// Keep the headers in an AVL tree sorted by memory address

typedef struct MemHeader {
    uint64_t sentinels1[_sentinelLen];
    struct MemHeader *left, *right;
    uint64_t sentinels2[_sentinelLen];
    uint32_t height;
    uint32_t line;
    const char *file; // assume static storage for file names
    size_t blockSize;
    uint64_t sentinels3[_sentinelLen];
} MemHeader;

// Code for PRNG found at https://stackoverflow.com/a/53900430/16275142

typedef struct PrngState {
    uint64_t state;
} PrngState;

static inline uint64_t _prngNext(PrngState *p) {
    uint64_t state = p->state;
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    p->state = state;
    return state * UINT64_C(2685821657736338717);
}

static MemHeader *g_memRoot = NULL;

static inline void _mhUpdateHeight(MemHeader *mh);
static inline int32_t _mhBalanceFactor(MemHeader *mh);
static MemHeader *_mhRotRight(MemHeader *oldRoot);
static MemHeader *_mhRotLeft(MemHeader *oldRoot);
static MemHeader *_mhRebalance(MemHeader *root);
static MemHeader *_mhInsert(MemHeader *root, MemHeader *mh);
static bool _mhContains(MemHeader *root, MemHeader *header);
static MemHeader *_mhMin(MemHeader *root);
static MemHeader *_mhRemove(MemHeader *root, MemHeader *mh);
static void _mhCheckIntegrity(MemHeader *header);
static bool _mhCheckBounds(MemHeader *header);
static void _mhPrint(MemHeader *header);
static void _mhPrintAll(MemHeader *root);

static void *_memAllocFilled(
    size_t byteCount,
    uint8_t val,
    uint32_t line,
    const char *file
);
static void *_memChangeBytesUnchecked(
    void *block,
    size_t newByteCount,
    uint32_t line,
    const char *file
);
static void _memFreeUnchecked(void *block);

static inline uint32_t _mhGetHeight(MemHeader *mh) {
    return mh ? mh->height : 0;
}

static inline void _mhUpdateHeight(MemHeader *mh) {
    uint32_t leftHeight = _mhGetHeight(mh->left);
    uint32_t rightHeight = _mhGetHeight(mh->right);
    mh->height = 1 + (leftHeight > rightHeight ? leftHeight : rightHeight);
}

static inline int32_t _mhBalanceFactor(MemHeader *mh) {
    return (int32_t)_mhGetHeight(mh->left) - (int32_t)_mhGetHeight(mh->right);
}

static MemHeader *_mhRotRight(MemHeader *oldRoot) {
    MemHeader *newRoot = oldRoot->left;
    MemHeader *tmp = newRoot->right;

    newRoot->right = oldRoot;
    oldRoot->left = tmp;

    _mhUpdateHeight(oldRoot);
    _mhUpdateHeight(newRoot);

    return newRoot;
}

static MemHeader *_mhRotLeft(MemHeader *oldRoot) {
    MemHeader *newRoot = oldRoot->right;
    MemHeader *tmp = newRoot->left;

    newRoot->left = oldRoot;
    oldRoot->right = tmp;

    _mhUpdateHeight(oldRoot);
    _mhUpdateHeight(newRoot);

    return newRoot;
}

static MemHeader *_mhRebalance(MemHeader *root) {
    int32_t balance = _mhBalanceFactor(root);

    if (balance > 1) {
        int32_t leftBalance = _mhBalanceFactor(root->left);
        if (leftBalance < 0) {
            root->left = _mhRotLeft(root->left);
        }
        return _mhRotRight(root);
    } else if (balance < -1) {
        int32_t rightBalance = _mhBalanceFactor(root->right);
        if (rightBalance > 0) {
            root->right = _mhRotRight(root->right);
        }
        return _mhRotLeft(root);
    } else {
        return root;
    }
}

static MemHeader *_mhInsert(MemHeader *root, MemHeader *mh) {
    if (root == NULL) {
        return mh;
    }

    memAssert(mh != root);
    _mhCheckIntegrity(root);
    if ((uintptr_t)mh < (uintptr_t)root) {
        root->left = _mhInsert(root->left, mh);
    } else {
        root->right = _mhInsert(root->right, mh);
    }
    _mhUpdateHeight(root);
    return _mhRebalance(root);
}

static bool _mhContains(MemHeader *root, MemHeader *header) {
    if (root == NULL) {
        return false;
    } else if ((uintptr_t)root == (uintptr_t)header) {
        _mhCheckIntegrity(root);
        return true;
    } else if ((uintptr_t)header < (uintptr_t)root) {
        _mhCheckIntegrity(root);
        return _mhContains(root->left, header);
    } else {
        _mhCheckIntegrity(root);
        return _mhContains(root->right, header);
    }
}

static MemHeader *_mhMin(MemHeader *root) {
    while (root->left) {
        root = root->left;
    }
    return root;
}

static MemHeader *_mhRemove(MemHeader *root, MemHeader *mh) {
    if ((uintptr_t)mh < (uintptr_t)root) {
        root->left = _mhRemove(root->left, mh);
    } else if ((uintptr_t)mh > (uintptr_t)root) {
        root->right = _mhRemove(root->right, mh);
    } else {
        if (!root->left) {
            return root->right;
        } else if (!root->right) {
            return root->left;
        }

        // Find smallest value of the right subtree and use it in place of root
        MemHeader *newRoot = _mhMin(root->right);
        newRoot->right = _mhRemove(root->right, newRoot);
        newRoot->left = root->left;
        root = newRoot;
    }
    _mhUpdateHeight(root);
    return _mhRebalance(root);
}

static void _mhCheckIntegrity(MemHeader *header) {
    for (int i = 0; i < _sentinelLen; i++) {
        if (header->sentinels1[i] != header->sentinels2[i]) {
            goto corruptionDetected;
        }
    }
    for (int i = 0; i < _sentinelLen; i++) {
        if (header->sentinels1[i] != header->sentinels3[i]) {
            goto corruptionDetected;
        }
    }
    return;

corruptionDetected:
    memLog("memory block corrupted, info not reliable\n");
    memLog("line: %"PRId32"\n", header->line);
    memLog("file: %.128s\n", header->file);
    abort();
}

static bool _mhCheckBounds(MemHeader *header) {
    _mhCheckIntegrity(header);
    return 0 == memcmp(
        header->sentinels1,
        (uint8_t *)(header + 1) + header->blockSize,
        sizeof(uint64_t) * _sentinelLen
    );
}

static void _mhPrint(MemHeader *header) {
    memLog(
        "%p - %s:%"PRIu32" - size=%zi\n",
        (void *)(header + 1),
        header->file,
        header->line,
        header->blockSize
    );
}

static void _mhPrintAll(MemHeader *root) {
    if (root == NULL) {
        return;
    }
    _mhCheckIntegrity(root);
    _mhPrintAll(root->left);
    _mhPrint(root);
    _mhPrintAll(root->right);
}

static void *_memAllocFilled(
    size_t byteCount,
    uint8_t val,
    uint32_t line,
    const char *file
) {
    MemHeader *block = malloc(
        sizeof(MemHeader) + byteCount + sizeof(uint64_t)*_sentinelLen
    );

    if (block == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    }

    block->line = line;
    block->file = file;
    block->blockSize = byteCount;
    block->height = 1;
    block->left = NULL;
    block->right = NULL;

    PrngState state = { (uintptr_t)block };
    void *tailSentinels = (uint8_t *)(block + 1) + byteCount;
    for (int i = 0; i < _sentinelLen; i++) {
        uint64_t sentinel = _prngNext(&state);
        block->sentinels1[i] = sentinel;
        block->sentinels2[i] = sentinel;
        block->sentinels3[i] = sentinel;
    }
    // cannot set tailSentinels[i] directly because the pointer might not be
    // aligned
    memcpy(tailSentinels, block->sentinels1, sizeof(uint64_t)*_sentinelLen);

    memset((void *)(block + 1), val, byteCount);

    g_memRoot = _mhInsert(g_memRoot, block);
    return (void *)(block + 1);
}

void *_memAlloc(
    size_t objectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
) {
    memAssert(threadMutexLock(&g_memMutex));
    const size_t size = objectSize * objectCount;
    // Detect overflow
    memAssert(size == 0 || size / objectCount == objectSize);
    void *block = _memAllocFilled(size, _garbageByte, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return block;
}

void *_memAllocBytes(size_t byteCount, uint32_t line, const char *file) {
    memAssert(threadMutexLock(&g_memMutex));
    void *block = _memAllocFilled(byteCount, _garbageByte, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return block;
}

void *_memAllocZeroed(
    size_t objectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
) {
    memAssert(threadMutexLock(&g_memMutex));
    const size_t size = objectSize * objectCount;
    // Detect overflow
    memAssert(size == 0 || size / objectCount == objectSize);
    void *block = _memAllocFilled(size, 0, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return block;
}

void *_memAllocZeroedBytes(size_t byteCount, uint32_t line, const char *file) {
    memAssert(threadMutexLock(&g_memMutex));
    void *block = _memAllocFilled(byteCount, 0, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return block;
}

static void *_memChangeBytesUnchecked(
    void *block,
    size_t byteCount,
    uint32_t line,
    const char *file
) {
    if (byteCount == 0) {
        _memFreeUnchecked(block);
        return NULL;
    }
    MemHeader *header = (MemHeader *)block - 1;
    void *newBlock = _memAllocFilled(byteCount, _garbageByte, line, file);

    // Always succeed when shrinking, this mirrors the common behaviour of
    // realloc even if it is not required.
    if (newBlock == NULL && (block == NULL || header->blockSize >= byteCount)) {
        return block;
    } else if (newBlock == NULL) {
        memFail("Out of memory.\n");
        return NULL;
    } else if (block == NULL) {
        return newBlock;
    }

    size_t minSize = byteCount < header->blockSize
        ? byteCount
        : header->blockSize;
    memcpy(newBlock, block, minSize);
    _memFreeUnchecked(block);
    return newBlock;
}

void *_memExpand(
    void *block,
    size_t newCount,
    size_t objectSize,
    uint32_t line,
    const char *file
) {
    const size_t size = objectSize * newCount;
    // Detect overflow
    memAssert(size == 0 || size / newCount == objectSize);
    return _memExpandBytes(block, size, line, file);
}

void *_memExpandBytes(
    void *block,
    size_t newByteCount,
    uint32_t line,
    const char *file
) {
    memAssert(newByteCount != 0);
    memAssert(threadMutexLock(&g_memMutex));
    MemHeader *header = (MemHeader *)block - 1;
    if (block != NULL && !_mhContains(g_memRoot, header)) {
        memLog("memExpand: invalid pointer\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        abort();
    }
    if (block != NULL) {
        _mhCheckIntegrity(header);
    }
    if (block != NULL && header->blockSize > newByteCount) {
        memLog("memExpand: new size (%zi) is smaller\n", newByteCount);
        memLog("   at %s:%"PRIu32"\n", file, line);
        _mhPrint(header);
        abort();
    }
    void *newBlock = _memChangeBytesUnchecked(block, newByteCount, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return newBlock;
}

void *_memShrink(
    void *block,
    size_t newCount,
    size_t objectSize,
    uint32_t line,
    const char *file
) {
    const size_t size = objectSize * newCount;
    // Detect overflow
    memAssert(size == 0 || size / newCount == objectSize);
    return _memShrinkBytes(block, size, line, file);
}

void *_memShrinkBytes(
    void *block,
    size_t newByteCount,
    uint32_t line,
    const char *file
) {
    if (block == NULL) {
        if (newByteCount == 0) {
            return NULL;
        } else {
            memLog("memShrink: expanding NULL ptr to (%zi)\n", newByteCount);
            memLog("   at %s:%"PRIu32"\n", file, line);
            abort();
        }
    }
    memAssert(threadMutexLock(&g_memMutex));
    MemHeader *header = (MemHeader *)block - 1;
    if (!_mhContains(g_memRoot, header)) {
        memLog("memShrink: invalid pointer\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        abort();
    }
    _mhCheckIntegrity(header);
    if (header->blockSize < newByteCount) {
        memLog("memShrink: new size (%zi) is bigger\n", newByteCount);
        memLog("   at %s:%"PRIu32"\n", file, line);
        _mhPrint(header);
        abort();
    }
    void *newBlock = _memChangeBytesUnchecked(block, newByteCount, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return newBlock;
}

void *_memChange(
    void *block,
    size_t objectCount,
    size_t objectSize,
    uint32_t line,
    const char *file
) {
    const size_t size = objectSize * objectCount;
    // Detect overflow
    memAssert(size == 0 || size / objectCount == objectSize);
    return _memChangeBytes(block, size, line, file);
}

void *_memChangeBytes(
    void *block,
    size_t byteCount,
    uint32_t line,
    const char *file
) {
    if (block == NULL) {
        return byteCount == 0 ? NULL : _memAllocBytes(byteCount, line, file);
    }

    memAssert(threadMutexLock(&g_memMutex));
    MemHeader *header = (MemHeader *)block - 1;
    if (!_mhContains(g_memRoot, header)) {
        memLog("memChange: invalid pointer\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        abort();
    }
    _mhCheckIntegrity(header);
    if (!_mhCheckBounds(header)) {
        memLog("memChange: out of bounds write\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        _mhPrint(header);
        abort();
    }

    void *newBlock = _memChangeBytesUnchecked(block, byteCount, line, file);
    memAssert(threadMutexUnlock(&g_memMutex));
    return newBlock;
}

static void _memFreeUnchecked(void *block) {
    if (block != NULL) {
        MemHeader *header = (MemHeader *)block - 1;
        g_memRoot = _mhRemove(g_memRoot, header);
        free(header);
    }
}

void _memFree(void *block, uint32_t line, const char *file) {
    if (block == NULL) {
        return;
    }
    memAssert(threadMutexLock(&g_memMutex));
    MemHeader *header = (MemHeader *)block - 1;
    if (!_mhContains(g_memRoot, header)) {
        memLog("memFree: invalid pointer\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        abort();
    }
    _mhCheckIntegrity(header);
    if (!_mhCheckBounds(header)) {
        memLog("memFree: out of bounds write\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        _mhPrint(header);
        abort();
    }
    _memFreeUnchecked(block);
    memAssert(threadMutexUnlock(&g_memMutex));
}

bool memHasAllocs(void) {
    return g_memRoot != NULL;
}

void memPrintAllocs(void) {
    memAssert(threadMutexLock(&g_memMutex));
    _mhPrintAll(g_memRoot);
    memAssert(threadMutexUnlock(&g_memMutex));
}

void _memCheckBounds(void *block, uint32_t line, const char *file) {
    if (block == NULL) {
        return;
    }
    memAssert(threadMutexLock(&g_memMutex));
    MemHeader *header = (MemHeader *)block - 1;
    if (!_mhContains(g_memRoot, header)) {
        memLog("memCheckBounds: invalid pointer\n");
        abort();
    }
    _mhCheckIntegrity(header);
    if (!_mhCheckBounds(header)) {
        memLog("memCheckBounds: out of bounds write\n");
        memLog("   at %s:%"PRIu32"\n", file, line);
        _mhPrint(header);
        abort();
    }
    memAssert(threadMutexUnlock(&g_memMutex));
}

bool memIsAlloc(void *block) {
    if (block == NULL) {
        return false;
    }

    MemHeader *header = (MemHeader *)block - 1;
    memAssert(threadMutexLock(&g_memMutex));
    bool result = _mhContains(g_memRoot, header);
    memAssert(threadMutexUnlock(&g_memMutex));
    return result;
}

void memFreeAllAllocs(void) {
    memAssert(threadMutexLock(&g_memMutex));
    while (g_memRoot != NULL) {
        _mhCheckIntegrity(g_memRoot);
        _memFreeUnchecked(g_memRoot + 1);
    }
    memAssert(threadMutexUnlock(&g_memMutex));
}

#endif // !CLIB_MEM_TRACE_ALLOCS
