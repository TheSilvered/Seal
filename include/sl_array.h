#ifndef SL_ARRAY_H_
#define SL_ARRAY_H_

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "clib_mem.h"

#define slArrayType(type, name, prefix)                                        \
    typedef struct name {                                                      \
        type *data;                                                            \
        uint32_t len, cap;                                                     \
    } name;                                                                    \
    bool prefix##Push(name *arr, type obj);                                    \
    type *prefix##At(name *arr, int64_t idx);                                  \
    type *prefix##At(name *arr, int64_t idx);                                  \
    void prefix##Clear(name *arr);

#define slArrayImpl(type, name, prefix)                                        \
    bool prefix##Push(name *arr, type obj) {                                   \
        assert(arr->len <= arr->cap);                                          \
        if (arr->len == arr->cap) {                                            \
            uint32_t newCap = arr->cap == 0 ? 1 : arr->cap * 2;                \
            type *newData = memExpand(arr->data, newCap, sizeof(*arr->data));  \
            if (newData == NULL) {                                             \
                return false;                                                  \
            }                                                                  \
            arr->data = newData;                                               \
            arr->cap = newCap;                                                 \
        }                                                                      \
        arr->data[arr->len++] = obj;                                           \
        return true;                                                           \
    }                                                                          \
    type *prefix##At(name *arr, int64_t idx) {                                 \
        if (idx < 0) {                                                         \
            idx += arr->len;                                                   \
        }                                                                      \
        if (idx < 0 || idx >= arr->len) {                                      \
            fprintf(                                                           \
                stderr,                                                        \
                "index %lli out of bounds (length %"PRIu32") for" #name "\n",  \
                idx, arr->len                                                  \
            );                                                                 \
            fflush(stderr);                                                    \
            abort();                                                           \
        }                                                                      \
        return &arr->data[(uint32_t)idx];                                      \
    }                                                                          \
    void prefix##Clear(name *arr) {                                            \
        arr->len = 0;                                                          \
        arr->cap = 0;                                                          \
        memFree(arr->data);                                                    \
        arr->data = NULL;                                                      \
    }

slArrayType(int32_t, i32Arr, i32)

#endif // !SL_ARRAY_H_
