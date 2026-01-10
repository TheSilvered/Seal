#ifndef SL_VM_H_
#define SL_VM_H_

#include <stdbool.h>

typedef struct SlVM {
    struct {
        bool occurred;
        char *msg;
    } error;
} SlVM;

#endif // !SL_VM_H_
