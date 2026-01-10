#ifndef SL_LEXER_H_
#define SL_LEXER_H_

#include <stdint.h>

#include "sl_vm.h"

typedef enum SlTokenKind {
    SlToken_Ident,
    SlToken_Int,
    SlToken_Add,

    // Keywords
    SlToken_KVar
} SlTokenKind;

typedef struct SlToken {
    SlTokenKind kind;
    uint32_t pos;
    union {
        struct {
            uint8_t *value;
            size_t len;
        } Ident, Str;
        int64_t Int;
    } as;
} SlToken;

typedef struct SlTokens {
    uint8_t *strs; // strings referenced by the tokens
    SlToken *tokens;
    size_t tokenCount;
} SlTokens;

SlTokens slTokenize(SlVM *vm, uint8_t *text, uint32_t len);

#endif // !SL_LEXER_H_
