#include "sl_vm.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

slArrayImpl(SlSource, SlSources, source)

SlSourceHandle slSourceFromCStr(SlVM *vm, const char *str) {
    size_t len = strlen(str);
    if (len > UINT32_MAX) {
        slSetError(
            vm,
            "%s: string too long, (max is %"PRIu32", got %zu)",
            __func__,
            UINT32_MAX,
            len
        );
        return -1;
    }
    const char *path = "<string>";
    uint8_t *textBuf = memAllocBytes(len);
    if (textBuf == NULL) {
        slSetOutOfMemoryError(vm);
        return -1;
    }
    memcpy(textBuf, str, len);
    SlSourceHandle hd = vm->sources.len;
    sourcePush(
        &vm->sources,
        (SlSource){
            .path = path,
            .text = textBuf,
            .textLen = len
        }
    );
    return hd;
}

SlSource slGetSource(SlVM *vm, SlSourceHandle hd) {
    if (hd < 0 || hd >= vm->sources.len) {
        slSetError(vm, "%s: invalid handle", __func__);
        return (SlSource){ .path = NULL, .text = NULL, .textLen = 0 };
    }
    return *sourceAt(&vm->sources, hd);
}

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

void slSetErrorVArg(SlVM *vm, const char *fmt, va_list args) {
    vsnprintf(vm->error.msg, sizeof(vm->error.msg), fmt, args);
    vm->error.occurred = true;
}
