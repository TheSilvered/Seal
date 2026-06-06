#include <assert.h>
#include <stdint.h>
#include <stdio.h>

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
    vm->curr.ip = mainFunc.as.func->proto->bytecode;
    vm->curr.stack = pushSlots(vm, mainFunc.as.func->proto->frameSize);
    vm->curr.shared = &mainFunc.as.func->sharedSlots[0];
    vm->curr.consts = mainFunc.as.func->proto->constants;
    if (vm->curr.stack == NULL) return false;

    SlCallFrame *frame = pushFrame(vm);
    if (frame == NULL) return false;
    frame->ip = NULL;
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

static inline void setSlot(SlVM *vm, uint16_t reg, SlObj obj) {
    if (vm->curr.stack[reg].type > SlObj_Str)
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

static bool pushFunc(SlVM *vm, uint16_t first, uint16_t paramCount) {
    SlObj func = vm->curr.stack[first];
    if (func.type != SlObj_Func) {
        slSetError(vm, "cannot call %s object", slTypeName(func));
        return false;
    }
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
    frame->ip = vm->curr.ip;
    frame->stackTop = vm->curr.stack;
    frame->retAddress = &vm->curr.stack[first];

    for (uint16_t i = 0; i < paramCount; i++) {
        stackTop[i] = slNewRef(vm->curr.stack[i + first + 1]);
    }

    vm->curr.ip = func.as.func->proto->bytecode;
    vm->curr.consts = func.as.func->proto->constants;
    vm->curr.stack = stackTop;
    vm->curr.shared = func.as.func->sharedSlots;

    return true;
}

static void funcReturn(SlVM *vm, SlObj val) {
    SlCallFrame *frame = topFrame(vm);

    slDelRef(*frame->retAddress);
    *frame->retAddress = val;

    vm->curr.ip = frame->ip;
    vm->curr.stack = frame->stackTop;

    popSlots(vm, frame->func->proto->frameSize);
    popFrame(vm);

    if (vm->callStack.totalUsed > 0) {
        frame = topFrame(vm);
        vm->curr.consts = frame->func->proto->constants;
        vm->curr.shared = frame->func->sharedSlots;
    }
}

#define getop(op)     ((SlOpCode)((op) >> 1 & 0x7f))
#define getrd(op, ex) ((uint16_t)(((ex) & 0xff00) | ((op) >> 8 & 0xff)))
#define getrdx(op)    ((uint16_t)((op) >> 8 & 0xffff))
#define getr1(op, ex) ((uint16_t)(((ex) >> 8 & 0xff00) | ((op) >> 16 & 0xff)))
#define getr1x(op)    ((uint16_t)((op) >> 16 & 0xffff))
#define getr2(op, ex) ((uint16_t)(((ex) >> 16 & 0xff00) | ((op) >> 24 & 0xff)))
#define getimmKs(op, ex)                                                       \
    (int16_t)((ex & 0xfe)                                                      \
        ? (uint16_t)(((ex) >> 16 & 0xff00) | ((op) >> 24 & 0xff))              \
        : (int8_t)((op) >> 24 & 0xff)                                          \
    )
#define getimmIu(op, ex) (uint32_t)(((ex) & 0xffffff00) | ((op) >> 24 & 0xff))
#define getimmIs(op, ex)                                                       \
    (int32_t)((ex & 0xfe)                                                      \
        ? (uint32_t)(((ex) & 0xffffff00) | ((op) >> 24 & 0xff))                \
        : (int8_t)((op) >> 24 & 0xff)                                          \
    )
#define getimmx(op) ((int32_t)((op >> 8 ^ 0x800000) - 0x800000))

#define binop(op, ex, func) do {                                               \
    SlObj o1 = vm->curr.stack[getr1((op), (ex))];                              \
    SlObj o2 = vm->curr.stack[getr2((op), (ex))];                              \
    SlObj result = (func)(vm, o1, o2);                                         \
    if (result.type == SlObj_Null) return false;                               \
    setSlot(vm, getrd((op), (ex)), result);                                    \
    } while (0)

#define binopi(op, ex, func) do {                                              \
    SlObj o = vm->curr.stack[getr1((op), (ex))];                               \
    SlObj imm = slObjInt(getimmKs((op), (ex)));                                \
    SlObj result = (func)(vm, o, imm);                                         \
    if (result.type == SlObj_Null) return false;                               \
    setSlot(vm, getrd((op), (ex)), result);                                    \
    } while (0)

#define tbinop(op, ex, func) do {                                              \
    SlObj o1 = vm->curr.stack[getr1((op), (ex))];                              \
    SlObj o2 = vm->curr.stack[getr2((op), (ex))];                              \
    SlObj result = (func)(vm, o1, o2);                                         \
    if (result.type == SlObj_Null) return false;                               \
    assert(result.type == SlObj_Bool);                                         \
    if (!result.as.boolean) vm->curr.ip++;                                     \
    } while (0)

#define tbinopi(op, ex, func) do {                                             \
    SlObj o = vm->curr.stack[getr1((op), (ex))];                               \
    SlObj imm = slObjInt(getimmKs((op), (ex)));                                \
    SlObj result = (func)(vm, o, imm);                                         \
    if (result.type == SlObj_Null) return false;                               \
    assert(result.type == SlObj_Bool);                                         \
    if (!result.as.boolean) vm->curr.ip++;                                     \
    } while (0)

const SlObj vals[] = { slFalse, slTrue, slNull };

static bool finishFunc(SlVM *vm) {
    assert(vm->callStack.totalUsed != 0);
    uint64_t finalStack = vm->callStack.totalUsed - 1;

    while (true) {
        // printf("ip = %zi\n", vm->curr.ip - topFrame(vm)->func->proto->bytecode);
        uint32_t op = *vm->curr.ip++;
        uint32_t ex = op & 1 ? *vm->curr.ip++ : 0;
        switch (getop(op)) {
        case SlOp_add: {
            binop(op, ex, slAdd);
            break;
        }
        case SlOp_addi: {
            binopi(op, ex, slAdd);
            break;
        }
        case SlOp_sub: {
            binop(op, ex, slSub);
            break;
        }
        case SlOp_subi: {
            binopi(op, ex, slSub);
            break;
        }
        case SlOp_mul: {
            binop(op, ex, slMul);
            break;
        }
        case SlOp_muli: {
            binopi(op, ex, slMul);
            break;
        }
        case SlOp_div:
        case SlOp_divi:
        case SlOp_mod:
        case SlOp_modi:
        case SlOp_pow:
        case SlOp_powi:
            assert(false && "TODO opcode (arith)");
            break;

        case SlOp_eq: {
            binop(op, ex, slEq);
            break;
        }
        case SlOp_eqi: {
            binopi(op, ex, slEq);
            break;
        }
        case SlOp_ne: {
            binop(op, ex, slNe);
            break;
        }
        case SlOp_nei: {
            binopi(op, ex, slNe);
            break;
        }
        case SlOp_lt:
        case SlOp_lti:
        case SlOp_le:
        case SlOp_lei:
        case SlOp_gt:
        case SlOp_gti:
        case SlOp_ge:
        case SlOp_gei:
            assert(false && "TODO opcode (ord)");
            break;

        case SlOp_mov: {
            SlObj o = vm->curr.stack[getr1x(op)];
            setSlot(vm, getrd(op, ex), slNewRef(o));
            break;
        }
        case SlOp_ldn: {
            uint16_t reg = getrdx(op);
            uint32_t count = getimmIu(op, ex);
            while (count--)
                setSlot(vm, reg++, slNull);
            break;
        }
        case SlOp_ldi: {
            setSlot(vm, getrdx(op), slObjInt(getimmIs(op, ex)));
            break;
        }
        case SlOp_ldv: {
            setSlot(vm, getrdx(op), vals[getimmIu(op, ex)]);
            break;
        }
        case SlOp_ldk: {
            setSlot(vm, getrdx(op), vm->curr.consts[getimmIu(op, ex)]);
            break;
        }
        case SlOp_ldsh: {
            setSlot(vm, getrdx(op), *vm->curr.shared[getimmIu(op, ex)]->value);
            break;
        }
        case SlOp_stsh: {
            SlSharedSlot *shared = vm->curr.shared[getimmIu(op, ex)];
            SlObj newVal = slNewRef(vm->curr.stack[getrdx(op)]);
            slDelRef(*shared->value);
            *shared->value = newVal;
            break;
        }
        case SlOp_mksh: {
            SlObj shared = slSharedSlotNew(vm, &vm->curr.stack[getr1x(op)]);
            setSlot(vm, getrd(op, ex), shared);
            break;
        }
        case SlOp_dtsh: {
            uint16_t reg = getrdx(op);
            uint32_t count = getimmIu(op, ex);
            while (count--)
                detachShared(vm->curr.stack[reg++]);
            break;
        }
        case SlOp_mkf: {
            SlObj func = makeClosure(vm, vm->curr.consts[getimmIu(op, ex)]);
            setSlot(vm, getrdx(op), func);
            break;
        }
        case SlOp_call: {
            pushFunc(vm, getrdx(op), getimmIu(op, ex));
            break;
        }
        case SlOp_tcall:
            assert(false && "TODO tcall");
            break;
        case SlOp_ret: {
            funcReturn(vm, vm->curr.stack[getrdx(op)]);
            if (vm->callStack.totalUsed <= finalStack) return true;
            break;
        }
        case SlOp_retv: {
            funcReturn(vm, vals[getimmIu(op, ex)]);
            if (vm->callStack.totalUsed <= finalStack) return true;
            break;
        }
        case SlOp_jmp: {
            vm->curr.ip += getimmx(op);
            break;
        }
        case SlOp_teq: {
            tbinop(op, ex, slEq);
            break;
        }
        case SlOp_teqi: {
            tbinopi(op, ex, slEq);
            break;
        }
        case SlOp_tne: {
            tbinop(op, ex, slNe);
            break;
        }
        case SlOp_tnei: {
            tbinopi(op, ex, slNe);
            break;
        }
        case SlOp_tlt:
        case SlOp_tlti:
        case SlOp_tle:
        case SlOp_tlei:
        case SlOp_tgt:
        case SlOp_tgti:
        case SlOp_tge:
        case SlOp_tgei:
            assert(false && "TODO opcode (ord test)");
            break;
        case SlOp_ttr: {
            SlObj o = vm->curr.stack[getrdx(op)];
            if (o.type == SlObj_Null || (o.type == SlObj_Bool && !o.as.boolean))
                vm->curr.ip++;
            break;
        }
        case SlOp_tfl: {
            SlObj o = vm->curr.stack[getrdx(op)];
            if (o.type != SlObj_Null && (o.type != SlObj_Bool || o.as.boolean))
                vm->curr.ip++;
            break;
        }
        case SlOp_tnl: {
            SlObj o = vm->curr.stack[getrdx(op)];
            if (o.type != SlObj_Null) vm->curr.ip++;
            break;
        }
        case SlOp_tnnl: {
            SlObj o = vm->curr.stack[getrdx(op)];
            if (o.type == SlObj_Null) vm->curr.ip++;
            break;
        }
        case SlOp_cget:
        case SlOp_cgeti:
        case SlOp_cgetk:
        case SlOp_cset:
        case SlOp_cseti:
        case SlOp_csetk:
            assert(false && "TODO opcode (container)");
            break;
        case SlOp_print: {
            SlObj o = vm->curr.stack[getrdx(op)];
            SlObj str = slToStr(vm, o);
            printf("%.*s\n", (int)str.as.str->len, (char *)str.as.str->bytes);
            break;
        }
        case SlOp_ext:
            assert(false && "unreachable");
        }
    }
}
