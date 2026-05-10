#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sl_builtin.h"
#include "sl_codegen.h"
#include "sl_exec.h"
#include "clib_mem.h"
#include "sl_vm.h"

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

static inline uint16_t decodeReg(SlVM *vm);
static inline uint8_t decodeU8(SlVM *vm);
static inline int8_t decodeI8(SlVM *vm);
static inline uint16_t decodeU16(SlVM *vm);
static inline uint32_t decodeU24(SlVM *vm);
static inline int32_t decodeI24(SlVM *vm);
// Set the value of a stack slot, a reference is taken from obj
static inline void setSlot(SlVM *vm, uint16_t reg, SlObj obj);

// Reset the virtual machine to run something new
static bool resetRuntime(SlVM *vm, SlObj mainFunc, SlObj *retAddr);

static bool finishFunc(SlVM *vm);

SlObj slRun(SlVM *vm, SlObj mainFunc) {
    if (mainFunc.type != SlObj_Func) {
        slSetError(vm, "slRun: 'mainFunc' is not a function");
        return slNull;
    }
    if (mainFunc.as.func->proto->paramCount != 0) {
        slSetError(vm, "slRun: 'mainFunc' cannot accept arguments");
        return slNull;
    }
    if (mainFunc.as.func->proto->sharedCount != 0) {
        slSetError(vm, "slRun: 'mainFunc' cannot capture any variables");
        return slNull;
    }

    SlObj res = slNull;
    resetRuntime(vm, mainFunc, &res);

    finishFunc(vm);

    return res;
}

static bool resetRuntime(SlVM *vm, SlObj mainFunc, SlObj *retAddr) {
    // Reset the call stack
    while (vm->callStack.top != NULL) {
        SlCallStackBlock *oldTop = vm->callStack.top;
        vm->callStack.top = oldTop->prev;
        memFree(oldTop);
    }
    vm->callStack.totalUsed = 0;

    // Reset the value stack
    while (vm->stackTop != NULL) {
        popSlots(vm, vm->stackTop->used); // Properly release the references
        SlStackBlock *oldTop = vm->stackTop;
        vm->stackTop = oldTop->prev;
        memFree(oldTop);
    }

    vm->error.occurred = false;
    vm->pc = 0;
    vm->curr.bytes = mainFunc.as.func->proto->bytes;
    vm->curr.stack = pushSlots(vm, mainFunc.as.func->proto->frameSize);
    vm->curr.shared = &mainFunc.as.func->sharedSlots[0];
    vm->curr.consts = mainFunc.as.func->proto->constants;
    if (vm->curr.stack == NULL) return false;

    SlCallFrame *frame = pushFrame(vm);
    if (frame == NULL) return false;
    frame->pc = 0;
    frame->func = mainFunc.as.func;
    frame->retAddress = retAddr;
    return true;
}

static SlObj *pushSlots(SlVM *vm, uint16_t count) {
    SlStackBlock *top = vm->stackTop;
    if (top == NULL || top->cap - top->used < count) {
        uint16_t newCap = count > _blockMinCapacity ? count : _blockMinCapacity;
        top = memAllocZeroedBytes(
            sizeof(*top) + newCap * sizeof(*top->slots)
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
            top->slots[i].type = SlObj_Empty;
        }
    }

    SlObj *first = &top->slots[top->used];
    top->used += count;
    return first;
}

static void popSlots(SlVM *vm, uint16_t count) {
    assert(vm->stackTop != NULL);
    if (vm->stackTop->used == 0) {
        SlStackBlock *block = vm->stackTop;
        vm->stackTop = block->prev;
        memFree(block);
    }
    uint16_t topUsed = vm->stackTop->used;
    assert(topUsed >= count);
    for (uint16_t i = 0; i < count; i++) {
        slDelRef(vm->stackTop->slots[topUsed - i - 1]);
    }
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

static inline uint8_t nextByteChecked(SlVM *vm) {
    assert(vm->pc < topFrame(vm)->func->proto->size);
    return vm->curr.bytes[vm->pc++];
}

static inline uint16_t decodeReg(SlVM *vm) {
    uint8_t byte0 = nextByteChecked(vm);
    if (byte0 <= 0x7f) {
        return byte0;
    }
    uint8_t byte1 = nextByteChecked(vm);
    return (((byte0 & 0x7f) << 8) | byte1) + 0x7f;
}

static inline uint8_t decodeU8(SlVM *vm) {
    return nextByteChecked(vm);
}

static inline int8_t decodeI8(SlVM *vm) {
    return (int8_t)nextByteChecked(vm);
}

static inline uint16_t decodeU16(SlVM *vm) {
    uint8_t byte0 = nextByteChecked(vm);
    uint8_t byte1 = nextByteChecked(vm);
    return ((uint16_t)byte0 << 8) | byte1;
}

static inline uint32_t decodeU24(SlVM *vm) {
    uint8_t byte0 = nextByteChecked(vm);
    uint8_t byte1 = nextByteChecked(vm);
    uint8_t byte2 = nextByteChecked(vm);

    return ((uint32_t)byte0 << 16) | ((uint32_t)byte1 << 8) | byte2;
}

static inline int32_t decodeI24(SlVM *vm) {
    assert((uint8_t)(-1) == 0xff);
    uint8_t byte0 = nextByteChecked(vm);
    uint8_t byte1 = nextByteChecked(vm);
    uint8_t byte2 = nextByteChecked(vm);

    return (int32_t)(
        ((uint32_t)(0 - (byte0 >> 7)) << 24)
        | ((uint32_t)byte0 << 16)
        | ((uint32_t)byte1 << 8)
        | byte2
    );
}

static inline void setSlot(SlVM *vm, uint16_t reg, SlObj obj) {
    slDelRef(vm->curr.stack[reg]);
    vm->curr.stack[reg] = obj;
}

static inline void detachShared(SlObj shared) {
    assert(shared.type == SlObj_SharedSlot);
    shared.as.sharedSlot->valCopy = slNewRef(*shared.as.sharedSlot->value);
}

static inline SlObj makeClosure(SlVM *vm, SlObj prototype) {
    SlObj func = slClosureFuncNew(vm, prototype);
    if (func.type == SlObj_Null) return func;

    SlSharedSlot **slots = func.as.func->sharedSlots;

    for (uint32_t i = 0; i < prototype.as.proto->sharedCount; i++) {
        SlSharedInfo info = prototype.as.proto->sharedInfo[i];
        if (info.fromShared) {
            slNewRef((SlObj){
                .type = SlObj_SharedSlot,
                .as.sharedSlot = vm->curr.shared[info.idx]
            });
            slots[i] = vm->curr.shared[info.idx];
        } else {
            assert(vm->curr.stack[info.idx].type == SlObj_SharedSlot);
            slNewRef(vm->curr.stack[info.idx]);
            slots[i] = vm->curr.stack[info.idx].as.sharedSlot;
        }
    }

    return func;
}

static bool pushFunc(SlVM *vm, uint16_t first, uint16_t last) {
    SlObj func = vm->curr.stack[first];
    if (func.type != SlObj_Func) {
        slSetError(vm, "cannot call %s object", slTypeName(func));
        return false;
    }
    uint16_t paramCount = last - first;
    if (paramCount != func.as.func->proto->paramCount) {
        slSetError(
            vm,
            "expected %u arguments but got %u",
            func.as.func->proto->paramCount,
            paramCount
        );
    }

    SlObj *stackTop = pushSlots(vm, func.as.func->proto->frameSize);
    if (stackTop == NULL) return false;

    SlCallFrame *frame = pushFrame(vm);
    if (frame == NULL) return false;

    frame->func = func.as.func;
    frame->pc = vm->pc;
    frame->stackTop = vm->curr.stack;
    frame->retAddress = &vm->curr.stack[first];

    for (uint16_t i = 0; i < paramCount; i++) {
        stackTop[i] = slNewRef(vm->curr.stack[i + first + 1]);
    }

    vm->pc = 0;
    vm->curr.bytes = func.as.func->proto->bytes;
    vm->curr.consts = func.as.func->proto->constants;
    vm->curr.stack = stackTop;
    vm->curr.shared = func.as.func->sharedSlots;

    return true;
}

static void funcReturn(SlVM *vm, SlObj val) {
    SlCallFrame *frame = topFrame(vm);

    slDelRef(*frame->retAddress);
    *frame->retAddress = val;

    vm->pc = frame->pc;
    vm->curr.stack = frame->stackTop;

    popSlots(vm, frame->func->proto->frameSize);
    popFrame(vm);

    if (vm->callStack.totalUsed > 0) {
        frame = topFrame(vm);
        vm->curr.bytes = frame->func->proto->bytes;
        vm->curr.consts = frame->func->proto->constants;
        vm->curr.shared = frame->func->sharedSlots;
    }
}

static bool finishFunc(SlVM *vm) {
    assert(vm->callStack.totalUsed != 0);
    uint64_t finalStack = vm->callStack.totalUsed - 1;

    while (true) {
        uint8_t op = nextByteChecked(vm);
        switch ((SlOpCode)op) {
        case SlOp_nop:
            break;
        case SlOp_ln: {
            uint16_t first = decodeReg(vm);
            uint16_t last = decodeReg(vm);
            for (uint16_t i = first; i <= last; i++) {
                slDelRef(vm->curr.stack[i]);
            }
            // (SlObj){ 0 } is slNull
            memset(
                &vm->curr.stack[first],
                0,
                (last - first + 1) * sizeof(SlObj)
            );
            break;
        }
        case SlOp_ltr:
            setSlot(vm, decodeReg(vm), slTrue);
            break;
        case SlOp_lfl:
            setSlot(vm, decodeReg(vm), slFalse);
            break;
        case SlOp_lb: {
            uint16_t dst = decodeReg(vm);
            int8_t value = decodeI8(vm);
            setSlot(vm, dst, slObjInt(value));
            break;
        }
        case SlOp_lkb: {
            uint16_t dst = decodeReg(vm);
            uint8_t idx = decodeU8(vm);
            setSlot(vm, dst, vm->curr.consts[idx]);
            break;
        }
        case SlOp_lks: {
            uint16_t dst = decodeReg(vm);
            uint16_t idx = decodeU16(vm);
            setSlot(vm, dst, vm->curr.consts[idx]);
            break;
        }
        case SlOp_lki: {
            uint16_t dst = decodeReg(vm);
            uint32_t idx = decodeU24(vm);
            setSlot(vm, dst, vm->curr.consts[idx]);
            break;
        }
        case SlOp_cpy: {
            uint16_t dst = decodeReg(vm);
            uint16_t src = decodeReg(vm);
            setSlot(vm, dst, slNewRef(vm->curr.stack[src]));
            break;
        }
        case SlOp_ls: {
            uint16_t dst = decodeReg(vm);
            uint16_t idx = decodeReg(vm);
            SlSharedSlot *shared = vm->curr.shared[idx];
            setSlot(vm, dst, slNewRef(*shared->value));
            break;
        }
        case SlOp_sts: {
            uint16_t idx = decodeReg(vm);
            uint16_t src = decodeReg(vm);
            SlSharedSlot *shared = vm->curr.shared[idx];
            SlObj newVal = slNewRef(vm->curr.stack[src]);
            slDelRef(*shared->value);
            *shared->value = newVal;
            break;
        }
        case SlOp_mks: {
            uint16_t dst = decodeReg(vm);
            uint16_t src = decodeReg(vm);
            SlObj slot = slSharedSlotNew(vm, &vm->curr.stack[src]);
            if (slot.type == SlObj_Null) return false;
            // Only shared slots can replace an attached shared slot since when
            // compiling the slots are occupied until the block ends and can
            // only be changed if the variable is shadowed
            if (vm->curr.stack[dst].type == SlObj_SharedSlot) {
                detachShared(vm->curr.stack[dst]);
            }
            setSlot(vm, dst, slot);
            break;
        }
        case SlOp_dts: {
            uint16_t first = decodeReg(vm);
            uint16_t last = decodeReg(vm);
            for (uint16_t i = first; i <= last; i++) {
                detachShared(vm->curr.stack[i]);
            }
            break;
        }
        case SlOp_add: {
            uint16_t dst = decodeReg(vm);
            uint16_t lhs = decodeReg(vm);
            uint16_t rhs = decodeReg(vm);

            SlObj result = slAdd(vm, vm->curr.stack[lhs], vm->curr.stack[rhs]);
            if (vm->error.occurred) return false;
            setSlot(vm, dst, result);
            break;
        }
        case SlOp_sub: {
            uint16_t dst = decodeReg(vm);
            uint16_t lhs = decodeReg(vm);
            uint16_t rhs = decodeReg(vm);

            SlObj result = slSub(vm, vm->curr.stack[lhs], vm->curr.stack[rhs]);
            if (vm->error.occurred) return false;
            setSlot(vm, dst, result);
            break;
        }
        case SlOp_mul: {
            uint16_t dst = decodeReg(vm);
            uint16_t lhs = decodeReg(vm);
            uint16_t rhs = decodeReg(vm);

            SlObj result = slMul(vm, vm->curr.stack[lhs], vm->curr.stack[rhs]);
            if (vm->error.occurred) return false;
            setSlot(vm, dst, result);
            break;
        }
        case SlOp_div:
        case SlOp_mod:
        case SlOp_pow:
        case SlOp_lt:
        case SlOp_le:
            assert(false && "TODO: opcode");
            return false;
        case SlOp_eq: {
            uint16_t dst = decodeReg(vm);
            uint16_t lhs = decodeReg(vm);
            uint16_t rhs = decodeReg(vm);

            SlObj result = slEq(vm, vm->curr.stack[lhs], vm->curr.stack[rhs]);
            if (vm->error.occurred) return false;
            setSlot(vm, dst, result);
            break;
        }
        case SlOp_ne: {
            uint16_t dst = decodeReg(vm);
            uint16_t lhs = decodeReg(vm);
            uint16_t rhs = decodeReg(vm);

            SlObj result = slNe(vm, vm->curr.stack[lhs], vm->curr.stack[rhs]);
            if (vm->error.occurred) return false;
            setSlot(vm, dst, result);
            break;
        }
        case SlOp_print: {
            SlObj val = vm->curr.stack[decodeReg(vm)];
            SlObj str = slToStr(vm, val);
            if (str.type == SlObj_Null) return false;
            printf("%.*s\n", (int)str.as.str->len, str.as.str->bytes);
            break;
        }
        case SlOp_mkfb: {
            uint16_t dst = decodeReg(vm);
            uint8_t idx = decodeU8(vm);
            SlObj func = makeClosure(vm, vm->curr.consts[idx]);
            if (func.type != SlObj_Func) return false;
            setSlot(vm, dst, func);
            break;
        }
        case SlOp_mkfs: {
            uint16_t dst = decodeReg(vm);
            uint16_t idx = decodeU16(vm);
            SlObj func = makeClosure(vm, vm->curr.consts[idx]);
            if (func.type != SlObj_Func) return false;
            setSlot(vm, dst, func);
            break;
        }
        case SlOp_mkfi: {
            uint16_t dst = decodeReg(vm);
            uint32_t idx = decodeU24(vm);
            SlObj func = makeClosure(vm, vm->curr.consts[idx]);
            if (func.type != SlObj_Func) return false;
            setSlot(vm, dst, func);
            break;
        }
        case SlOp_call: {
            uint16_t first = decodeReg(vm);
            uint16_t last = decodeReg(vm);
            if (!pushFunc(vm, first, last)) return false;
            break;
        }
        case SlOp_tcall:
            assert(false && "TODO: tcall");
            return false;
        case SlOp_ret: {
            SlObj val = vm->curr.stack[decodeReg(vm)];
            funcReturn(vm, slNewRef(val));
            if (vm->callStack.totalUsed <= finalStack) {
                return true;
            }
            break;
        }
        case SlOp_retnl: {
            funcReturn(vm, slNull);
            if (vm->callStack.totalUsed <= finalStack) {
                return true;
            }
            break;
        }
        case SlOp_jmp: {
            int32_t diff = decodeI24(vm);
            vm->pc += diff;
            break;
        }
        case SlOp_jtr: {
            SlObj cond = vm->curr.stack[decodeReg(vm)];
            int32_t diff = decodeI24(vm);
            if (slIsTruthy(cond)) {
                vm->pc += diff;
            }
            break;
        }
        case SlOp_jfl: {
            SlObj cond = vm->curr.stack[decodeReg(vm)];
            int32_t diff = decodeI24(vm);
            if (!slIsTruthy(cond)) {
                vm->pc += diff;
            }
            break;
        }
        case SlOp_jlt:
        case SlOp_jle:
        case SlOp_jeq:
        case SlOp_jne:
            assert(false && "TODO: cond jmp");
            return false;
        }
    }
}
