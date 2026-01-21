#ifndef SL_VM_H_
#define SL_VM_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct SlSrc {
    char *path;
    uint8_t *text;
    uint32_t textLen;
} SlSrc;

typedef int32_t SlSrcHandle;

typedef struct SlVM {
    struct {
        bool occurred;
        char msg[512];
    } error;
    SlSrc *sources;
} SlVM;

SlSrcHandle slSrcFromCStr(const char *str);

void slSetOutOfMemoryError(SlVM *vm);
void slSetError(SlVM *vm, const char *fmt, ...);

#endif // !SL_VM_H_
