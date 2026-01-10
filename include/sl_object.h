#ifndef SL_OBJECT_H_
#define SL_OBJECT_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SlObjIsNumeric(type) (((type) >> 1) == 0x1)
#define SlObjIsSequence(type) (((type) >> 2) == 0x1)

typedef struct SlObj SlObj;

typedef enum SlObjType {
    SlObj_Null      = 0b00000000,
    SlObj_Bool      = 0b00000001,
    SlObj_Int       = 0b00000010,
    SlObj_Float     = 0b00000011,
    SlObj_Tuple     = 0b00000100,
    SlObj_List      = 0b00000101,
    SlObj_Bytesview = 0b00000110,
    SlObj_Bytes     = 0b00000111,
    SlObj_Char      = 0b00001000,
    SlObj_Str       = 0b00001001,
    SlObj_Strview   = 0b00001010,
    SlObj_Struct    = 0b00001011,
    SlObj_Func      = 0b00001100,
    SlObj_Map       = 0b00001101,
    SlObj_Iter      = 0b00001110,
    SlObj_Foreign   = 0b00001111
} SlObjType;

typedef struct SlTuple SlTuple;
typedef struct SlList SlList;
typedef struct SlBytesview SlBytesview;
typedef struct SlBytes SlBytes;
typedef struct SlStr SlStr;
typedef struct SlStrview SlStrview;
typedef struct SlStruct SlStruct;
typedef struct SlMap SlMap;
typedef struct SlFunc SlFunc;
typedef struct SlIter SlIter;
typedef struct SlForeign SlForeign;

typedef struct SlObj {
    SlObjType type;
    union {
        const bool         Bool;
        const uint64_t     Int;
        const double       Float;
        const uint32_t     Char;
        const SlTuple     *Tuple;
        const SlList      *List;
        const SlBytesview *Bytesview;
        const SlBytes     *Bytes;
        const SlStr       *Str;
        const SlStrview   *Strview;
        const SlStruct    *Struct;
        const SlMap       *Map;
        const SlFunc      *Func;
        const SlIter      *Iter;
        const SlForeign   *Foreign;
    } as;
} SlObj;

typedef struct SlGCObj {
    size_t refCount;
} SlGCObj;

struct SlList {
    SlGCObj asGCObj;
    SlObj *objs;
    size_t len, cap;
};

struct SlTuple {
    SlGCObj asGCObj;
    union {
        SlObj *objs;
        size_t idx;
    } data;
    size_t len;
    SlList *ref;
};

struct SlBytes {
    SlGCObj asGCObj;
    uint8_t *bytes;
    size_t len, cap;
};

struct SlBytesview {
    SlGCObj asGCObj;
    union {
        uint8_t *bytes;
        size_t idx;
    } data;
    size_t len;
    SlBytes *ref;
};

struct SlStr {
    SlGCObj asGCObj;
    uint8_t *chars;
    size_t len, cap;
};

struct SlStrview {
    SlGCObj asGCObj;
    union {
        uint8_t *chars;
        size_t idx;
    } data;
    size_t len;
    SlStr *ref;
};

typedef struct SlMapEntry {
    SlObj key, value;
} SlMapEntry;

struct SlStruct {
    SlGCObj asGCObj;
    SlMapEntry entries;
    size_t len, cap;
};

struct SlMap {
    SlGCObj asGCObj;
    SlMapEntry entries;
    size_t len, cap;
};

struct SlFunc {
    SlGCObj asGCObj;
    // TODO: function object fields
};

typedef bool (*SlIterNext)(SlIter *iter, SlObj *next);

struct SlIter {
    SlGCObj asGCObj;
    SlIterNext *nextFn;
    void *data;
};

struct SlForeign {
    SlGCObj asGCObj;
    // TODO: foreign object fields + foreign types
};

// Get an Int object
SlObj slObjInt(uint64_t value);
// Get a Float object
SlObj slObjFloat(double value);
// Get a Bool object
SlObj slObjBool(bool value);
// Get a Char object
SlObj slObjChar(int32_t value);

#endif // !SL_OBJECT_H_
