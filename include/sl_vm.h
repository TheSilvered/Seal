#ifndef SL_VM_H_
#define SL_VM_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define slObjIsSmall(obj) ((obj).type <= SlObj_Float)
#define slObjIsNumeric(obj)                                                    \
    ((obj).type == SlObj_Int || (obj).type == SlObj_Float)

typedef enum SlObjType {
    // Small objects (not tracked by gc, stored inline)

    SlObj_Null,
    SlObj_Empty, // internal (undefined value)
    SlObj_StackIdx, // internal (an index on the stack)
    SlObj_Bool,
    SlObj_Int,
    SlObj_Float,

    // Acyclic objects (tracked by the gc but cannot contain themselves)

    SlObj_Str,
    SlObj_Prototype, // internal (function prototype)

    // Cyclic objects (objects that can contain themselves)

    SlObj_List,
    SlObj_Map,
    SlObj_Func,
    SlObj_Struct,
    SlObj_SharedSlot, // internal (value captured by closures)

    // Frozen types

    SlObj_FrozenList = SlObj_List | 0x800, // a.k.a. tuple
    SlObj_FrozenStr  = SlObj_Str  | 0x800,
    SlObj_FrozenMap  = SlObj_Map  | 0x800
} SlObjType;

typedef bool SlBool;
typedef int64_t SlInt;
typedef double SlFloat;
typedef struct SlList SlList;
typedef struct SlStr SlStr;
typedef struct SlMap SlMap;
typedef struct SlFunc SlFunc;
typedef struct SlStruct SlStruct;
typedef struct SlPrototype SlPrototype;
typedef struct SlSharedSlot SlSharedSlot;

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
        SlPrototype *proto;
        SlSharedSlot *sharedSlot;
        uint16_t stackIdx;
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
    SlPrototype *proto;
    SlSharedSlot *sharedSlots[];
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

typedef struct SlSlotInfo {
    uint8_t *name;
    uint16_t slotIdx;
} SlSlotInfo;

// `path` and `name` are allocated after the SlDebugInfo struct and end with NUL,
// `lineInfo` and `slotInfo` are allocated separately.
typedef struct SlDebugInfo {
    uint8_t *path;
    uint8_t *name;
    SlLineInfo *lineInfo;
    SlSlotInfo *slotInfo;
    uint32_t lineInfoCount;
    uint16_t slotInfoCount;
} SlDebugInfo;

typedef struct SlSharedInfo {
    bool fromShared;
    uint16_t idx;
} SlSharedInfo;

struct SlPrototype {
    SlGCObj asGCObj;
    uint8_t *bytes;
    uint32_t size;
    uint32_t constCount;
    SlObj *constants;
    SlDebugInfo *debugInfo;
    uint16_t frameSize;
    uint16_t sharedCount;
    SlSharedInfo *sharedInfo;
};

struct SlSharedSlot {
    SlGCObj asGCObj;
    SlObj value;
};

typedef struct SlSource {
    const char *path;
    uint8_t *text;
    uint32_t textLen;
} SlSource;

typedef struct SlStackBlock {
    struct SlStackBlock *prev;
    uint16_t used, cap;
    SlObj slots[];
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
    SlPrototype *bytecode;
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

// Create a new function prototype object.
// Ownership of bytes, constants, sharedInfo and debugInfo is transferred to
// the new object.
// If an error occurs return NULL.
SlObj slPrototypeNew(
    SlVM *vm,
    uint8_t *bytes,
    uint32_t size,
    SlObj *constants,
    uint32_t constCount,
    SlSharedInfo *sharedInfo,
    uint16_t sharedCount,
    uint16_t frameSize,
    SlDebugInfo *debugInfo
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
