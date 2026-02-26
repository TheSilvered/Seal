#ifndef SL_VM_H_
#define SL_VM_H_

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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
        SlGCObj *structure;
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

typedef struct SlLineInfo {
    size_t start;
    uint32_t len;
    uint32_t line;
} SlLineInfo;

typedef struct SlDebugInfo {
    SlStr *path;
    SlLineInfo *lineInfo;
    size_t lineInfoCount;
    uint8_t **slotNames;
    uint16_t nameCount;
} SlDebugInfo;

struct SlBytecode {
    SlGCObj asGCObj;
    uint8_t *bytes;
    uint32_t size;
    uint16_t frameSize;
    uint32_t constantCount;
    SlObj *constants;
    SlDebugInfo *debugInfo;
};

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
// The length is capped at UINT32_MAX even if the string is longer.
SlSource slSourceFromCStr(const char *str);

// Add `count` slots to the stack and return a pointer to the first.
SlObj *slPushSlots(SlVM *vm, uint16_t count);
// Remove `count` slots from the stack. Each call must undo a previous
// `slPushSlots` call with the same number of slots.
void slPopSlots(SlVM *vm, uint16_t count);

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

// Create a new string object.
// The contents of bytes are copied.
// If an error occurs return NULL.
SlStr *slFrozenStrNew(
    SlVM *vm,
    const uint8_t *bytes,
    size_t len
);

// Create a new function object.
// A reference is taken from name and bytecode.
// If an error occurs return NULL.
SlFunc *slFrozenFuncNew(
    SlVM *vm,
    SlStr *name,
    SlBytecode *bytecode
);

// Create a new bytecode object.
// All data in bytes and constants is copied, the new object takes a reference
// from all objects in constants.
// Ownership of debugInfo is transfered to the new object.
// If an error occurs return NULL.
SlBytecode *slBytecodeNew(
    SlVM *vm,
    const uint8_t *bytes,
    uint32_t size,
    uint16_t frameSize,
    const SlObj *constants,
    uint32_t constantCount,
    const SlDebugInfo *debugInfo
);

void slSetOutOfMemoryError(SlVM *vm);
void slSetError(SlVM *vm, const char *fmt, ...);
void slSetErrorVArg(SlVM *vm, const char *fmt, va_list args);

#endif // !SL_VM_H_
