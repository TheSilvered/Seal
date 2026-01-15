#ifndef SL_VM_H_
#define SL_VM_H_

#include <stdbool.h>

typedef struct SlVM {
    struct {
        bool occurred;
        char msg[512];
    } error;
} SlVM;

void slSetOutOfMemoryError(SlVM *vm);
void slSetError(SlVM *vm, const char *fmt, ...);

#endif // !SL_VM_H_
