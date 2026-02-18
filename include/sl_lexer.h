#ifndef SL_LEXER_H_
#define SL_LEXER_H_

#include <stdint.h>
#include <stddef.h>

#include "sl_vm.h"

typedef enum SlTokenKind {
    SlToken_Ident,
    SlToken_NumInt,
    SlToken_Plus,
    SlToken_Star,
    SlToken_Hyphen,
    SlToken_FwSlash,
    SlToken_Perc,
    SlToken_Comma,
    SlToken_Colon,
    SlToken_Semicolon,
    SlToken_LeftParen,
    SlToken_RightParen,
    SlToken_LeftSquare,
    SlToken_RightSquare,
    SlToken_LeftCurly,
    SlToken_RightCurly,
    SlToken_Equals,

    SlToken_KwVar,
    SlToken_KwFunc,

    SlToken_Eof
} SlTokenKind;

typedef struct SlStrIdx {
    uint32_t idx, len;
} SlStrIdx;

typedef struct SlToken {
    SlTokenKind kind;
    uint32_t line;
    union {
        SlStrIdx ident;
        int64_t numInt;
    } as;
} SlToken;

typedef struct SlTokens {
    uint8_t *strs; // strings referenced by the tokens
    SlToken *tokens;
    size_t tokenCount;
} SlTokens;

SlTokens slTokenize(SlVM *vm, SlSource *source);
const char *slTokenKindToStr(SlTokenKind kind);

#endif // !SL_LEXER_H_
