#include "sl_vm.h"
#include "clib_mem.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <assert.h>

#define _blockMinCapacity 512 // 8 KiB blocks

SlSource slSourceFromCStr(const char *str) {
    size_t len = strlen(str);
    if (len > UINT32_MAX) {
        len = UINT32_MAX;
    }
    return (SlSource) {
        .path = "<string>",
        .text = (uint8_t *)str,
        .textLen = len
    };
}

SlObj *slPushSlots(SlVM *vm, uint16_t count) {
    assert(count != 0);

    SlStackBlock *top = vm->stackTop;
    if (top == NULL || top->cap - top->used < count) {
        uint16_t newCap = count > _blockMinCapacity ? count : _blockMinCapacity;
        SlStackBlock *newBlock = memAllocBytes(
            sizeof(*newBlock) + (newCap - 1) * sizeof(*newBlock->slots)
        );
        if (newBlock == NULL) {
            slSetOutOfMemoryError(vm);
            return NULL;
        }
        newBlock->prev = top;
        newBlock->cap = newCap;
        newBlock->used = 0;
        vm->stackTop = newBlock;
    }

    SlObj *first = &top->slots[top->used];
    top->used += count;
    return first;
}

void slPopSlots(SlVM *vm, uint16_t count) {
    assert(count != 0);
    assert(vm->stackTop != NULL);
    if (vm->stackTop->used == 0) {
        SlStackBlock *block = vm->stackTop;
        vm->stackTop = block->prev;
        memFree(block);
    }
    assert(vm->stackTop->used >= count);
    vm->stackTop->used -= count;
}

SlObj slObjInt(uint64_t value) {
    return (SlObj) {
        .type = SlObj_Int,
        .as.numInt = value
    };
}

SlObj slObjFloat(double value) {
    return (SlObj) {
        .type = SlObj_Int,
        .as.numFloat = value
    };
}

SlStr *slFrozenStrNew(
    SlVM *vm,
    const uint8_t *bytes,
    size_t len
) {
    SlStr *str = memAllocBytes(sizeof(*str) + len * sizeof(*bytes));
    if (str == NULL) {
        slSetOutOfMemoryError(vm);
        return NULL;
    }

    str->asGCObj.refCount = 1;
    str->bytes = (uint8_t *)(str + 1);
    str->len = len;
    str->cap = 0;
    memcpy(str->bytes, bytes, len * sizeof(*bytes));

    return str;
}

SlFunc *slFrozenFuncNew(
    SlVM *vm,
    SlStr *name,
    SlBytecode *bytecode
) {
    SlFunc *func = memAlloc(1, sizeof(*func));
    if (func == NULL) {
        slSetOutOfMemoryError(vm);
        return NULL;
    }

    func->asGCObj.refCount = 1;
    func->name = name;
    func->bytecode = bytecode;
    func->sharedSlots = NULL;

    return func;
}

SlBytecode *slBytecodeNew(
    SlVM *vm,
    const uint8_t *bytes,
    uint32_t size,
    uint16_t frameSize,
    const SlObj *constants,
    uint32_t constantCount,
    const SlDebugInfo *debugInfo
) {
    // Object layout:
    // | SlBytecode struct |
    // | Constants array   |
    // | Bytecode          |
    SlBytecode *bc = memAllocBytes(
        sizeof(*bc)
        + sizeof(*constants) * constantCount
        + sizeof(*bytes) * size
    );

    if (bc == NULL) {
        slSetOutOfMemoryError(vm);
        return NULL;
    }

    bc->asGCObj.refCount = 1;
    bc->bytes = (uint8_t *)(bc + 1) + (sizeof(*constants) * constantCount);
    bc->size = size;
    bc->frameSize = frameSize;
    bc->constants = (SlObj *)(bc + 1);
    bc->constantCount = constantCount;
    bc->debugInfo = debugInfo;

    memcpy(bc->bytes, bytes, size * sizeof(*bytes));
    memcpy(bc->constants, constants, constantCount * sizeof(*constants));

    return bc;
}

void slSetOutOfMemoryError(SlVM *vm) {
    const char msg[] = "Out of memory.";
    memcpy(vm->error.msg, msg, sizeof(msg));
    vm->error.occurred = true;
}

void slSetError(SlVM *vm, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(vm->error.msg, sizeof(vm->error.msg), fmt, args);
    va_end(args);
    vm->error.occurred = true;
}

void slSetErrorVArg(SlVM *vm, const char *fmt, va_list args) {
    vsnprintf(vm->error.msg, sizeof(vm->error.msg), fmt, args);
    vm->error.occurred = true;
}
