#include "sl_builtin.h"
#include "sl_codegen.h"
#include "sl_exec.h"
#include "clib_mem.h"

#define _blockMinCapacity 512 // 8 KiB blocks

// Add `count` slots to the stack and return a pointer to the first.
static SlObj *pushSlots(SlVM *vm, uint16_t count);
// Remove `count` slots from the stack. Each call must undo a previous
// `slPushSlots` call with the same number of slots.
static void popSlots(SlVM *vm, uint16_t count);

// Add a stack frame to the call stack.
static SlCallFrame *pushFrame(SlVM *vm);
// Get top call frame
static SlCallFrame *topFrame(SlVM *vm);
// Remove a frame from the call stack.
static void popFrame(SlVM *vm);

static bool callFunc(SlVM *vm, SlObj func, SlObj *retAddress);
static bool exeFunc(SlVM *vm);
static inline uint16_t decodeReg(SlVM *vm);
static inline uint32_t decodeU32(SlVM *vm);
// Set the value of a stack slot, a reference is taken from obj
static inline void setSlot(SlVM *vm, uint16_t reg, SlObj obj);

SlObj slRun(SlVM *vm, SlObj mainFunc) {
    SlObj res = slNull;
    callFunc(vm, mainFunc, &res);

    exeFunc(vm);

    return res;
}

static SlObj *pushSlots(SlVM *vm, uint16_t count) {
    assert(count != 0);

    SlStackBlock *top = vm->stackTop;
    if (top == NULL || top->cap - top->used < count) {
        uint16_t newCap = count > _blockMinCapacity ? count : _blockMinCapacity;
        top = memAllocZeroedBytes(
            sizeof(*top) + (newCap - 1) * sizeof(*top->slots)
        );
        if (top == NULL) {
            slSetOutOfMemoryError(vm);
            return NULL;
        }
        top->prev = top;
        top->cap = newCap;
        top->used = 0;
        vm->stackTop = top;
        for (uint16_t i = 0; i < newCap; i++) {
            top->slots[i].type = SlObj_EmptySlot;
        }
    }

    SlObj *first = &top->slots[top->used];
    top->used += count;
    return first;
}

static void popSlots(SlVM *vm, uint16_t count) {
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

static SlCallFrame *pushFrame(SlVM *vm) {
    vm->callStack.totalUsed++;
    SlCallStackBlock *top = vm->callStack.top;
    if (top != NULL && top->used < slCallStackCap) {
        return &top->frames[top->used++];
    }
    SlCallStackBlock *block = memAlloc(1, sizeof(*block));
    if (block == NULL) {
        slSetOutOfMemoryError(vm);
        return NULL;
    }
    block->prev = top;
    vm->callStack.top = block;
    block->used = 1;
    return &block->frames[0];
}

static SlCallFrame *topFrame(SlVM *vm) {
    assert(vm->callStack.top != NULL);
    assert(vm->callStack.totalUsed > 0);
    SlCallStackBlock *top = vm->callStack.top;
    if (top->used > 0) {
        return &top->frames[top->used - 1];
    } else {
        return &top->prev->frames[top->prev->used - 1];
    }
}

static void popFrame(SlVM *vm) {
    assert(vm->callStack.top != NULL);
    assert(vm->callStack.totalUsed > 0);
    vm->callStack.totalUsed--;
    SlCallStackBlock *top = vm->callStack.top;
    if (top->used > 0) {
        top->used--;
    } else {
        vm->callStack.top = top->prev;
        vm->callStack.top->used--;
        memFree(top);
    }
}

static bool callFunc(SlVM *vm, SlObj func, SlObj *retAddress) {
    SlCallFrame *frame = pushFrame(vm);
    if (frame == NULL) {
        return false;
    }

    if ((func.type & 0xff) != SlObj_Func) {
        slSetError(vm, "only functions can be called");
        return false;
    }

    frame->pc = vm->pc;
    frame->func = func.as.func;
    frame->retAddress = retAddress;
    vm->bytecode = func.as.func->bytecode;
    vm->sharedSlots = func.as.func->sharedSlots;
    vm->pc = 0;
    vm->stackPtr = pushSlots(vm, vm->bytecode->frameSize);
    return true;
}

static bool exeFunc(SlVM *vm) {
    assert(vm->callStack.totalUsed > 0);
    uint64_t initialSize = vm->callStack.totalUsed;

    while (vm->callStack.totalUsed >= initialSize) {
        assert(vm->pc < vm->bytecode->size);
        uint8_t op = vm->bytecode->bytes[vm->pc++];
        switch (op) {
        case SlOp_nop:
            break;
        case SlOp_ldnull:
            setSlot(vm, decodeReg(vm), slNull);
            break;
        case SlOp_ldi8: {
            uint16_t dest = decodeReg(vm);
            SlObj num = slObjInt((int8_t)vm->bytecode->bytes[vm->pc++]);
            setSlot(vm, dest, num);
            break;
        }
        case SlOp_cpy: {
            uint16_t dest, src;
            dest = decodeReg(vm);
            src = decodeReg(vm);
            setSlot(vm, dest, slNewRef(vm->stackPtr[src]));
        }
        case SlOp_add: {
            uint16_t dest, lhs, rhs;
            dest = decodeReg(vm);
            lhs = decodeReg(vm);
            rhs = decodeReg(vm);
            setSlot(vm, dest, slAdd(vm, vm->stackPtr[lhs], vm->stackPtr[rhs]));
            goto maybeError;
        }
        case SlOp_sub:
        case SlOp_mul:
        case SlOp_div:
        case SlOp_mod:
        case SlOp_pow: {
            slSetError(vm, "TODO: implement opcode");
            goto maybeError;
        }
        case SlOp_print: {
            SlObj str = slToStr(vm, vm->stackPtr[decodeReg(vm)]);
            if ((str.type & 0xff) != SlObj_Str) {
                goto maybeError;
            }
            printf("%.*s\n", (int)str.as.str->len, (char *)str.as.str->bytes);
            slDelRef(str);
            break;
        }
        case SlOp_ret: {
            SlObj retVal = slNewRef(vm->stackPtr[decodeReg(vm)]);
            SlCallFrame *frame = topFrame(vm);
            slDelRef(*frame->retAddress);
            *frame->retAddress = retVal;
            vm->pc = frame->pc;
            popFrame(vm);
            break;
        }
        default:
            assert(false && "unreachable opcode");
            return false;
        }

        continue;
    maybeError:
        if (vm->error.occurred) {
            return false;
        }
    }
    return true;
}

static inline uint16_t decodeReg(SlVM *vm) {
    assert(vm->pc < vm->bytecode->size);
    uint8_t byte0 = vm->bytecode->bytes[vm->pc++];
    if (byte0 <= 0x7f) {
        return byte0;
    }
    assert(vm->pc < vm->bytecode->size);
    uint8_t byte1 = vm->bytecode->bytes[vm->pc++];
    return (((byte0 & 0x7f) << 8) | byte1) + 0x7f;
}

static inline uint32_t decodeU32(SlVM *vm) {
    uint32_t val = (vm->bytecode->bytes[vm->pc + 0] << 24)
                 | (vm->bytecode->bytes[vm->pc + 1] << 16)
                 | (vm->bytecode->bytes[vm->pc + 2] << 8)
                 | (vm->bytecode->bytes[vm->pc + 3]);
    vm->pc += 4;
    return val;
}

static inline void setSlot(SlVM *vm, uint16_t reg, SlObj obj) {
    slDelRef(vm->stackPtr[reg]);
    vm->stackPtr[reg] = obj;
}
