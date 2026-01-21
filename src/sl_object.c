#include "sl_object.h"

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
