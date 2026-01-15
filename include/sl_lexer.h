#ifndef SL_LEXER_H_
#define SL_LEXER_H_

#include <stdint.h>
#include <stddef.h>

#include "sl_vm.h"

typedef enum SlTokenKind {
    SlToken_Ident,
    SlToken_Int,
    SlToken_Add
} SlTokenKind;

typedef struct SlToken {
    SlTokenKind kind;
    uint32_t pos;
    union {
        struct {
            uint8_t *value;
            uint32_t len;
        } ident, str;
        int64_t intLiteral;
    } as;
} SlToken;

typedef struct SlTokens {
    uint8_t *strs; // strings referenced by the tokens
    SlToken *tokens;
    size_t tokenCount;
} SlTokens;

SlTokens slTokenize(SlVM *vm, uint8_t *text, uint32_t len);

#endif // !SL_LEXER_H_
