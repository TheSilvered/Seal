#ifndef SL_ARRAY_H_
#define SL_ARRAY_H_

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "clib_mem.h"
#include "sl_vm.h"

#define slArrayType(Type, Name, prefix)                                        \
    typedef struct Name {                                                      \
        Type *data;                                                            \
        uint32_t len, cap;                                                     \
    } Name;                                                                    \
    bool prefix##Push(SlVM *vm, Name *arr, Type obj);                          \
    Type *prefix##At(Name *arr, int64_t idx);                                  \
    void prefix##Clear(Name *arr);

#define slArrayImpl(Type, Name, prefix)                                        \
    bool prefix##Push(SlVM *vm, Name *arr, Type obj) {                         \
        assert(arr->len <= arr->cap);                                          \
        if (arr->len == arr->cap) {                                            \
            uint32_t newCap = arr->cap == 0 ? 1 : arr->cap * 2;                \
            Type *newData = memExpand(arr->data, newCap, sizeof(*arr->data));  \
            if (newData == NULL) {                                             \
                slSetOutOfMemoryError(vm);                                     \
                return false;                                                  \
            }                                                                  \
            arr->data = newData;                                               \
            arr->cap = newCap;                                                 \
        }                                                                      \
        arr->data[arr->len++] = obj;                                           \
        return true;                                                           \
    }                                                                          \
    Type *prefix##At(Name *arr, int64_t idx) {                                 \
        if (idx < 0) {                                                         \
            idx += arr->len;                                                   \
        }                                                                      \
        if (idx < 0 || idx >= arr->len) {                                      \
            fprintf(                                                           \
                stderr,                                                        \
                "index %"PRIi64" out of bounds (length %"PRIu32") for "        \
                #Name "\n",                                                    \
                idx, arr->len                                                  \
            );                                                                 \
            fflush(stderr);                                                    \
            abort();                                                           \
        }                                                                      \
        return &arr->data[(uint32_t)idx];                                      \
    }                                                                          \
    void prefix##Clear(Name *arr) {                                            \
        arr->len = 0;                                                          \
        arr->cap = 0;                                                          \
        memFree(arr->data);                                                    \
        arr->data = NULL;                                                      \
    }

slArrayType(int32_t, SlI32Arr, slI32)
slArrayType(uint8_t, SlU8Arr, slU8)

typedef struct SlStrIdx {
    uint32_t idx, len;
} SlStrIdx;

slArrayType(SlStrIdx, SlStrArr, slStr)

#endif // !SL_ARRAY_H_
