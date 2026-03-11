#ifndef SL_VM_H_
#define SL_VM_H_

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "sl_array.h"

#define slObjIsSmall(obj) ((obj).type <= SlObj_Float)
#define slObjIsNumeric(obj)                                                    \
    ((obj).type == SlObj_Int || (obj).type == SlObj_Float)

typedef enum SlObjType {
    // Small objects (not tracked by gc)

    SlObj_Null,
    SlObj_EmptySlot, // internal
    SlObj_Bool,
    SlObj_Int,
    SlObj_Float,

    // Acyclic objects (tracked by the gc but cannot contain themselves)

    SlObj_Str,
    SlObj_Bytecode, // internal

    // Cyclic objects (objects that can contain themselves)

    SlObj_List,
    SlObj_Map,
    SlObj_Func,
    SlObj_Struct,
    SlObj_SharedSlots, // internal

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
    uint32_t reserved;
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
        SlGCObj *gcObj;
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

typedef struct SlMethodTable {
    struct SlMethodTable *nextMt;

    SlObj (*type)(void);
    void (*destructor)(void *gcObj);
} SlMethodTable;

struct SlStruct {
    SlGCObj asGCObj;
    SlMethodTable *mt;
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
    const SlDebugInfo *debugInfo;
};

struct SlSharedSlots {
    SlGCObj asGCObj;
    size_t slotCount;
    SlObj slots[1];
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

typedef struct SlCallFrame {
    SlFunc *func;
    uint64_t pc;
    SlObj *retAddress;
} SlCallFrame;

#define slCallStackCap 32

typedef struct SlCallStackBlock {
    struct SlCallStackBlock *prev;
    SlCallFrame frames[slCallStackCap];
    uint64_t used;
} SlCallStackBlock;

typedef struct SlCallStack {
    SlCallStackBlock *top;
    uint64_t totalUsed;
} SlCallStack;

// Seal virtual machine, init with `SlVM vm = { 0 };`
typedef struct SlVM {
    struct {
        bool occurred;
        char msg[512];
    } error;
    SlMethodTable *mtTop;
    SlStackBlock *stackTop;
    SlCallStack callStack;
    uint64_t pc;
    SlBytecode *bytecode;
    SlSharedSlots *sharedSlots;
    SlObj *stackPtr;
} SlVM;

// Create a source from a C string. No memory is allocated.
// The length is capped at UINT32_MAX even if the string is longer.
SlSource slSourceFromCStr(const char *str);
// Allocate a source from a file. It can be freed with slSourceFree
SlSource *slSourceFromFile(SlVM *vm, const char *path);
void slSourceFree(SlSource *source);

#define slNull ((SlObj){                                                       \
        .type = SlObj_Null,                                                    \
    })

#define slTrue ((SlObj){                                                       \
        .type = SlObj_Bool,                                                    \
        .as.boolean = true                                                     \
    })

#define slFalse ((SlObj){                                                      \
        .type = SlObj_Bool,                                                    \
        .as.boolean = false                                                    \
    })

SlObj slObjInt(int64_t value);
SlObj slObjFloat(double value);

// Create a new string object.
// The contents of bytes are copied.
// If an error occurs return NULL.
SlObj slFrozenStrNew(
    SlVM *vm,
    const uint8_t *bytes,
    size_t len
);

SlObj slFrozenStrFmt(SlVM *vm, const char *fmt, ...);

// Create a new function object.
// A reference is taken from name and bytecode.
// If an error occurs return NULL.
SlObj slFrozenFuncNew(
    SlVM *vm,
    SlObj name,
    SlObj bytecode
);

// Create a new bytecode object.
// All data in bytes and constants is copied, the new object takes a reference
// from all objects in constants.
// Ownership of debugInfo is transfered to the new object.
// If an error occurs return NULL.
SlObj slBytecodeNew(
    SlVM *vm,
    const uint8_t *bytes,
    uint32_t size,
    uint16_t frameSize,
    const SlObj *constants,
    uint32_t constantCount,
    const SlDebugInfo *debugInfo
);

// Get a new reference to an object.
SlObj slNewRef(SlObj o);
// Delete a reference of an object.
void slDelRef(SlObj o);

// Get the name of a type.
const char *slTypeName(SlObj o);

void slSetOutOfMemoryError(SlVM *vm);
void slSetError(SlVM *vm, const char *fmt, ...);
void slSetErrorVArg(SlVM *vm, const char *fmt, va_list args);

#endif // !SL_VM_H_
