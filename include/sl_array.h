#ifndef SL_ARRAY_H_
#define SL_ARRAY_H_

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include "clib_mem.h"

#define SL_ARRAY_TYPE(type, name) \
    typedef struct name { \
        type *data; \
        uint32_t len, cap; \
    } name; \

#define SL_ARRAY_SRC(type, name, prefix) \
    bool prefix##Push(name *arr, type obj) { \
        if (arr->len == arr->cap) { \
            uint32_t newCap = arr->cap == 0 ? 1 : (uint32_t)(arr->cap * 1.5); \
            type *newData = memExpand(arr->data, newCap, sizeof(*arr->data)); \
            if (newData == NULL) { \
                return false; \
            } \
            arr->data = newData; \
            arr->cap = newCap; \
        } \
        arr->data[arr->len++] = obj; \
        return true; \
    } \
    type *prefix##At(name *arr, int64_t idx) { \
        if (idx < 0) { \
            idx += arr->len; \
        } \
        if (idx < 0 || idx >= arr->len) { \
            fprintf( \
                stderr, \
                "index %lli out of bounds (length %"PRIu32") for" #name "\n", \
                idx, arr->len \
            ); \
            fflush(stderr); \
            abort(); \
        } \
        return &arr->data[(uint32_t)idx]; \
    } \
    void prefix##Clear(name *arr) { \
        arr->len = 0; \
        arr->cap = 0; \
        memFree(arr->data); \
        arr->data = NULL; \
    }

#endif // !SL_ARRAY_H_
