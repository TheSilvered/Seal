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
    SlObj_Struct,

    // Internal types

    SlObj_Bytecode,
    SlObj_SharedSlots,
    SlObj_EmptySlot,

    // Frozen types

    SlObj_FrozenList = SlObj_List | 0x800, // tuple
    SlObj_FrozenStr  = SlObj_Str  | 0x800,
    SlObj_FrozenMap  = SlObj_Map  | 0x800,
    SlObj_FrozenFunc = SlObj_Func | 0x800  // function prototype
} SlObjType;

typedef bool SlBool;
typedef int64_t SlInt;
typedef double SlFloat;
typedef struct SlList SlList;
typedef struct SlStr SlStr;
typedef struct SlMap SlMap;
typedef struct SlFunc SlFunc;
typedef struct SlStruct SlStruct;
typedef struct SlBytecode SlBytecode;
typedef struct SlSharedSlots SlSharedSlots;

typedef struct SlGCObj {
    size_t refCount;
} SlGCObj;

typedef struct SlObj {
    uint32_t type;
    uint32_t mtIdx; // method table index
    union {
        SlBool boolean;
        SlInt numInt;
        SlFloat numFloat;
        SlList *list;
        SlStr *str;
        SlMap *map;
        SlFunc *func;
        SlStruct *structure;
        SlBytecode *bytecode;
        SlSharedSlots *sharedSlots;
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
    SlStr *name;
    SlBytecode *bytecode;
    SlSharedSlots *sharedSlots;
};

struct SlStruct {
    SlGCObj asGCObj;
    uint64_t value;
};

typedef struct SlLineInfo {
    size_t start;
    uint32_t len;
    uint32_t line;
} SlLineInfo;

struct SlBytecode {
    SlGCObj asGCObj;
    uint8_t *bytes;
    size_t size;
    SlStr *path;
    SlLineInfo *lineInfo;
    size_t lineInfoCount;
    SlObj *constants;
};

struct SlSharedSlots {
    SlGCObj asGCObj;
    size_t slotCount;
    SlObj slots[1];
};

#define slNull ((SlObj){                                                       \
        .type = SlObj_Null,                                                    \
        .mtIdx = 0                                                             \
    })

#define slTrue ((SlObj){                                                       \
        .type = SlObj_Bool,                                                    \
        .mtIdx = 0,                                                            \
        .as.boolean = true                                                     \
    })

#define slFalse ((SlObj){                                                      \
        .type = SlObj_Bool,                                                    \
        .mtIdx = 0,                                                            \
        .as.boolean = false                                                    \
    })

SlObj slObjInt(uint64_t value);
SlObj slObjFloat(double value);

#endif // !SL_OBJECT_H_
