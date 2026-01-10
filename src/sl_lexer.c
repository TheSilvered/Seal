#include "sl_lexer.h"

#include <limits.h>
#include <stdbool.h>

typedef struct SlLexerState {
    uint8_t *strs;
    uint8_t *strsEnd;
    size_t strsCap;

    SlToken *tokens;
    size_t tokensLen;
    size_t tokensCap;

    uint32_t pos;
} SlLexerState;

static bool appendToken(SlLexerState *l, SlToken token);

SlTokens slTokenize(uint8_t *text, uint32_t len) {
    SlLexerState l = {
        .strs = NULL,
        .strsEnd = NULL,
        .strsCap = 0,
        .tokens = NULL,
        .tokensLen = 0,
        .tokensCap = 0,
        .pos = 0
    };

    for (l.pos = 0; l.pos < len; l.pos++) {
        uint8_t ch = text[l.pos];
        if (ch == '+') {
            appendToken(&l, (SlToken) { .kind = SlToken_Add });
        }
    }

    return (SlTokens) {
        .strs = l.strs,
        .tokens = l.tokens,
        .tokenCount = l.tokensLen
    };
}

static bool appendToken(SlLexerState *l, SlToken token) {

}

