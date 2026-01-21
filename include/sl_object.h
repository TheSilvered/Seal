#ifndef SL_OBJECT_H_
#define SL_OBJECT_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SlObjIsNumeric(type) (((type) >> 1) == 0x1)
#define SlObjIsSequence(type) (((type) >> 2) == 0x1)

typedef struct SlObj SlObj;

typedef enum SlObjType {
    SlObj_Null,
    SlObj_Bool,
    SlObj_Int,
    SlObj_Float,
    SlObj_List,
    SlObj_Str,
    SlObj_Map,
    SlObj_Func,
    SlObj_Struct
} SlObjType;

typedef bool SlBool;
typedef int64_t SlInt;
typedef double SlFloat;
typedef struct SlList SlList;
typedef struct SlStr SlStr;
typedef struct SlMap SlMap;
typedef struct SlFunc SlFunc;
typedef struct SlStruct SlStruct;

typedef struct SlGCObj {
    size_t refCount;
} SlGCObj;

typedef struct SlObj {
    bool frozen; // frozen flag
    uint8_t type; // object type
    uint16_t mtIdx; // method table index
    uint32_t extra; // any extra data that is constant per-instantce
    union {
        SlBool boolean;
        SlInt numInt;
        SlFloat numFloat;
        SlList *list;
        SlStr *str;
        SlMap *map;
        SlFunc *func;
        SlStruct *structure;
    } as;
} SlObj;

struct SlList {
    SlGCObj asGCObj;
    SlObj *objs;
    size_t len, cap;
};

struct SlStr {
    SlGCObj asGCObj;
    uint8_t *bytes;
    size_t len, cap;
};

typedef struct SlMapEntry {
    SlObj key, value;
} SlMapEntry;

struct SlMap {
    SlGCObj asGCObj;
    SlMapEntry *entries;
    size_t len, cap;
};

struct SlFunc {
    SlGCObj asGCObj;
};

struct SlStruct {
    SlGCObj asGCObj;
    uint64_t value;
};

#define slNull ((SlObj){                                                       \
        .frozen = true,                                                        \
        .type = SlObj_Null,                                                    \
        .mtIdx = 0                                                             \
    })

#define slTrue ((SlObj){                                                       \
        .frozen = true,                                                        \
        .type = SlObj_Bool,                                                    \
        .mtIdx = 0,                                                            \
        .as.boolean = true                                                     \
    })

#define slFalse ((SlObj){                                                      \
        .frozen = true,                                                        \
        .type = SlObj_Bool,                                                    \
        .mtIdx = 0,                                                            \
        .as.boolean = false                                                    \
    })

SlObj slObjInt(uint64_t value);
SlObj slObjFloat(double value);

#endif // !SL_OBJECT_H_
