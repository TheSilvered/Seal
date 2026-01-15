#include "sl_vm.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

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
