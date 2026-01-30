#include "sl_vm.h"
#include "clib_mem.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <assert.h>

#define _blockMinCapacity 512 // 8 KiB blocks

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
