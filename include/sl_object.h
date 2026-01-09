#ifndef SL_OBJECT_H_
#define SL_OBJECT_H_

#include <stdint.h>
#include <stddef.h>

#define SlObjIsNumeric(type) (((type) >> 1) == 0x1)
#define SlObjIsSequence(type) (((type) >> 2) == 0x1)

typedef enum SlObjType {
    SlObj_Null      = 0b0000,
    SlObj_Bool      = 0b0001,
    SlObj_Int       = 0b0010,
    SlObj_Float     = 0b0011,
    SlObj_Tuple     = 0b0100,
    SlObj_List      = 0b0101,
    SlObj_Bytesview = 0b0110,
    SlObj_Bytes     = 0b0111,
    SlObj_Char      = 0b1000,
    SlObj_Str       = 0b1001,
    SlObj_Strview   = 0b1010,
    SlObj_Struct    = 0b1011,
    SlObj_Function  = 0b1100,
    SlObj_Map       = 0b1101,
    SlObj_Foreign   = 0b1110,
} SlObjType;

typedef struct SlObj {
    uint16_t flags;
    uint16_t type;
    size_t refCount;
} SlObj;

typedef struct SlInt {
    SlObj obj;
    const int64_t value;
} SlInt;

typedef struct SlFloat {
    SlObj obj;
    const double value;
} SlFloat;

typedef struct SlTuple {
    SlObj obj;
    const SlObj *const *const objs;
    const size_t len;
    const bool owned;
} SlTuple;

// typedef struct SlStruct
// typedef struct SlFunction

typedef struct SlStrview {
    SlObj obj;
    bool owned;
    uint8_t *str;
    size_t len;
} SlStrview;

typedef struct SlBytesview {
    SlObj obj;
    bool owned;
    uint8_t *bytes;
    size_t len;
} SlBytesview;

typedef struct SlStr {
    SlObj obj;
    uint8_t *str;
    size_t len, cap;
} SlStr;

typedef struct SlBytes {
    SlObj obj;
    uint8_t *bytes;
    size_t len, cap;
} SlBytes;

typedef struct SlList {
    SlObj obj;
    SlObj **objs;
    size_t len, cap;
} SlList;

// typedef struct SlMap

#endif // !SL_OBJECT_H_
