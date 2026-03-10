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
