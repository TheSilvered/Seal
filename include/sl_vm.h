#ifndef SL_VM_H_
#define SL_VM_H_

#include "sl_array.h"

typedef struct SlSource {
    char *path;
    uint8_t *text;
    uint32_t textLen;
} SlSource;

slArrayType(SlSource, SlSources);
typedef int32_t SlSourceHandle;

typedef struct SlVM {
    struct {
        bool occurred;
        char msg[512];
    } error;
    SlSources sources;
} SlVM;

// Create a source from a C string.
// Return the handle on success and -1 on failure.
SlSourceHandle slSourceFromCStr(SlVM *vm, const char *str);
// Get a source file.
// If the handle is invalid `path` and `text` will be `NULL` and `textLen` 0.
SlSource slGetSource(SlVM *vm, SlSourceHandle hd);

void slSetOutOfMemoryError(SlVM *vm);
void slSetError(SlVM *vm, const char *fmt, ...);

#endif // !SL_VM_H_
