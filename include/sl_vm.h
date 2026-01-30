#ifndef SL_VM_H_
#define SL_VM_H_

#include "sl_object.h"

typedef struct SlSource {
    const char *path;
    uint8_t *text;
    uint32_t textLen;
} SlSource;

typedef struct SlStackBlock {
    struct SlStackBlock *prev;
    uint16_t used, cap;
    SlObj slots[1];
} SlStackBlock;

typedef struct SlVM {
    struct {
        bool occurred;
        char msg[512];
    } error;
    SlStackBlock *stackTop;
} SlVM;

// Create a source from a C string. No memory is allocated.
// The length caps at UINT32_MAX even if the string is longer.
SlSource slSourceFromCStr(const char *str);

// Add `count` slots to the stack and return a pointer to the first.
SlObj *slPushSlots(SlVM *vm, uint16_t count);
// Remove `count` slots from the stack. Each call must undo a previous
// `slPushSlots` call with the same number of slots.
void slPopSlots(SlVM *vm, uint16_t count);

void slSetOutOfMemoryError(SlVM *vm);
void slSetError(SlVM *vm, const char *fmt, ...);
void slSetErrorVArg(SlVM *vm, const char *fmt, va_list args);

#endif // !SL_VM_H_
