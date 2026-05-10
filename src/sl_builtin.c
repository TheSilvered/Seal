#include <assert.h>
#include <inttypes.h>

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
            "%s + %s is not supported",
            slTypeName(a), slTypeName(b)
        );
        return slNull;
    }
}

SlObj slSub(SlVM *vm, SlObj a, SlObj b){
    if (slObjIsNumeric(a) && slObjIsNumeric(b)) {
        if (a.type == SlObj_Int && b.type == SlObj_Int) {
            return slObjInt(a.as.numInt - b.as.numInt);
        }
        SlFloat valA = a.type == SlObj_Int
            ? (SlFloat)a.as.numInt
            : a.as.numFloat;
        SlFloat valB = b.type == SlObj_Int
            ? (SlFloat)b.as.numInt
            : b.as.numFloat;
        return slObjFloat(valA - valB);
    } else {
        slSetError(
            vm,
            "%s - %s is not supported",
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

SlObj slEq(SlVM *vm, SlObj a, SlObj b) {
    if (slObjIsNumeric(a) && slObjIsNumeric(b)) {
        if (a.type == SlObj_Int && b.type == SlObj_Int) {
            return a.as.numInt == b.as.numInt ? slTrue : slFalse;
        }
        SlFloat valA = a.type == SlObj_Int
            ? (SlFloat)a.as.numInt
            : a.as.numFloat;
        SlFloat valB = b.type == SlObj_Int
            ? (SlFloat)b.as.numInt
            : b.as.numFloat;
        return valA == valB ? slTrue : slFalse;
    } else if (a.type != b.type) {
        return slFalse;
    } else if (a.type == SlObj_Bool) {
        return a.as.boolean == b.as.boolean ? slTrue : slFalse;
    } else if (a.type == SlObj_Null) {
        return slTrue;
    } else {
        slSetError(
            vm,
            "%s == %s not supported",
            slTypeName(a), slTypeName(b)
        );
        return slNull;
    }
}

SlObj slNe(SlVM *vm, SlObj a, SlObj b) {
    SlObj eq = slEq(vm, a, b);
    if (eq.type == SlObj_Null) return slNull;
    return eq.as.boolean ? slFalse : slTrue;
}

SlObj slToStr(SlVM *vm, SlObj o) {
#define SlU8(s) (const uint8_t *)(s), sizeof(s) - 1

    switch (o.type & 0xff) {
    case SlObj_Null:
        return slFrozenStrNew(vm, SlU8("null"));
    case SlObj_Empty:
        return slFrozenStrNew(vm, SlU8("<internal:empty>"));
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
    case SlObj_Prototype:
        return slFrozenStrNew(vm, SlU8("<internal:prototype>"));
    default:
        assert(false && "TODO slToStr");
        return slFrozenStrNew(vm, SlU8("TODO"));
    }
#undef SlU8
}

bool slIsTruthy(SlObj o) {
    return o.type != SlObj_Null && (o.type != SlObj_Bool || o.as.boolean);
}
