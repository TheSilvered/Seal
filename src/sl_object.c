#include "sl_object.h"

SlObj slObjNull(void) {
    return (SlObj) {
        .type = SlObj_Null
    };
}

SlObj slObjInt(uint64_t value) {
    return (SlObj) {
        .type = SlObj_Int,
        .as.Int = value
    };
}

SlObj slObjFloat(double value) {
    return (SlObj) {
        .type = SlObj_Int,
        .as.Float = value
    };
}

SlObj slObjBool(bool value) {
    return (SlObj) {
        .type = SlObj_Bool,
        .as.Bool = value
    };
}

SlObj slObjChar(int32_t value) {
    return (SlObj) {
        .type = SlObj_Char,
        .as.Char = value
    };
}

