#include "sl_vm.h"

SlObj slAdd(SlVM *vm, SlObj a, SlObj b);
SlObj slSub(SlVM *vm, SlObj a, SlObj b);
SlObj slMul(SlVM *vm, SlObj a, SlObj b);
SlObj slEq(SlVM *vm, SlObj a, SlObj b);
SlObj slNe(SlVM *vm, SlObj a, SlObj b);
SlObj slToStr(SlVM *vm, SlObj obj);

bool slIsTruthy(SlObj o);
