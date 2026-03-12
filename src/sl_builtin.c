#include "sl_builtin.h"
#include "sl_vm.h"

SlObj slAdd(SlVM *vm, SlObj a, SlObj b){
    if (slObjIsNumeric(a) && slObjIsNumeric(b)) {
        if (a.type == SlObj_Int && b.type == SlObj_Int) {
            return slObjInt(a.as.numInt + b.as.numInt);
        }
        SlFloat valA = a.type == SlObj_Int
            ? (SlFloat)a.as.numInt
            : a.as.numFloat;
        SlFloat valB = b.type == SlObj_Int
            ? (SlFloat)b.as.numInt
            : b.as.numFloat;
        return slObjFloat(valA + valB);
    } else {
        slSetError(
            vm,
            "%s + %s not supported",
            slTypeName(a), slTypeName(b)
        );
        return slNull;
    }
}

SlObj slMul(SlVM *vm, SlObj a, SlObj b) {
    if (slObjIsNumeric(a) && slObjIsNumeric(b)) {
        if (a.type == SlObj_Int && b.type == SlObj_Int) {
            return slObjInt(a.as.numInt * b.as.numInt);
        }
        SlFloat valA = a.type == SlObj_Int
            ? (SlFloat)a.as.numInt
            : a.as.numFloat;
        SlFloat valB = b.type == SlObj_Int
            ? (SlFloat)b.as.numInt
            : b.as.numFloat;
        return slObjFloat(valA * valB);
    } else {
        slSetError(
            vm,
            "%s + %s not supported",
            slTypeName(a), slTypeName(b)
        );
        return slNull;
    }
}

SlObj slToStr(SlVM *vm, SlObj o) {
#define SlU8(s) (const uint8_t *)(s), sizeof(s) - 1

    switch (o.type & 0xff) {
    case SlObj_Null:
        return slFrozenStrNew(vm, SlU8("null"));
    case SlObj_EmptySlot:
        return slFrozenStrNew(vm, SlU8("internal:empty_slot"));
    case SlObj_Bool:
        if (o.as.boolean) {
            return slFrozenStrNew(vm, SlU8("true"));
        } else {
            return slFrozenStrNew(vm, SlU8("false"));
        }
    case SlObj_Int:
        return slFrozenStrFmt(vm, "%"PRIi64, o.as.numInt);
    case SlObj_Float:
        return slFrozenStrFmt(vm, "%.15g", o.as.numFloat);
    case SlObj_Str:
        return slNewRef(o);
    case SlObj_Bytecode:
        return slFrozenStrNew(vm, SlU8("internal:bytecode"));
    default:
        assert(false && "TODO slToStr");
        return slFrozenStrNew(vm, SlU8("TODO"));
    }
#undef SlU8
}
