#include "sl_lexer.h"
#include "clib_mem.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

typedef struct SlLexerState {
    SlVM *vm;

    uint8_t *text;
    uint32_t len;

    uint8_t *strs;
    uint32_t strsLen;
    uint32_t strsCap;

    SlToken *tokens;
    uint32_t tokensLen;
    uint32_t tokensCap;

    uint32_t pos;
} SlLexerState;

static bool appendToken(SlLexerState *l, SlToken token);
static uint8_t *appendStr(SlLexerState *l, uint8_t *str, uint32_t len);
static bool appendNumber(SlLexerState *l);
static bool appendIdent(SlLexerState *l);

SlTokens slTokenize(SlVM *vm, uint8_t *text, uint32_t len) {
    SlLexerState l = {
        .vm = vm,
        .text = text,
        .len = len,
        .strs = NULL,
        .strsLen = 0,
        .strsCap = 0,
        .tokens = NULL,
        .tokensLen = 0,
        .tokensCap = 0,
        .pos = 0
    };

    for (l.pos = 0; l.pos < len; l.pos++) {
        uint8_t ch = text[l.pos];
        bool success = true;
        if (isspace(ch)) {
            continue;
        } else if (ch == '+') {
            success = appendToken(&l, (SlToken) { .kind = SlToken_Add });
        } else if (isdigit(ch)) {
            success = appendNumber(&l);
        } else if (isalpha(ch) || ch == '_') {
            success = appendIdent(&l);
        } else {
            if (isprint(ch))
                slSetError(vm, "invalid character %c", ch);
            else {
                slSetError(vm, "invalid byte 0x%02x", ch);
            }
            success = false;
        }

        if (!success) {
            memFree(l.tokens);
            memFree(l.strs);
            return (SlTokens) {
                .strs = NULL,
                .tokens = NULL,
                .tokenCount = 0
            };
        }
    }

    return (SlTokens) {
        .strs = l.strs,
        .tokens = l.tokens,
        .tokenCount = l.tokensLen
    };
}

static bool appendToken(SlLexerState *l, SlToken token) {
    if (l->tokensCap == l->tokensLen) {
        uint32_t newCap = l->tokensCap == 0 ? 128 : l->tokensCap * 2;
        SlToken *newTokens = memChange(l->tokens, newCap, sizeof(*l->tokens));
        if (newTokens == NULL) {
            slSetOutOfMemoryError(l->vm);
            return false;
        }
        l->tokens = newTokens;
        l->tokensCap = newCap;
    }
    l->tokens[l->tokensLen++] = token;
    return true;
}

static uint8_t *appendStr(SlLexerState *l, uint8_t *str, uint32_t len) {
    if (l->strsLen + len > l->strsCap) {
        uint32_t newCap = (len + l->strsLen) * 2;
        uint8_t *newStrs = memChange(l->strs, newCap, sizeof(*l->strs));
        if (newStrs == NULL) {
            slSetOutOfMemoryError(l->vm);
            return NULL;
        }
        l->strs = newStrs;
        l->strsCap = newCap;
    }
    uint8_t *outPtr = l->strs + l->strsLen;
    memcpy(outPtr, str, len);
    l->strsLen += len;
    return outPtr;
}

static bool appendNumber(SlLexerState *l) {
    int64_t n = 0;
    uint32_t pos = l->pos;
    while (l->pos < l->len && isdigit(l->text[l->pos])) {
        n = n*10 + (l->text[l->pos] - '0');
        l->pos++;
    }
    l->pos--;
    return appendToken(
        l,
        (SlToken){ .kind = SlToken_Int, .pos = pos, .as.intLiteral = n }
    );
}

static bool appendIdent(SlLexerState *l) {
    uint32_t start = l->pos;

    while (
        l->pos < l->len
        && (isalnum(l->text[l->pos]) || l->text[l->pos] == '_')
    ) {
        l->pos++;
    }

    uint32_t len = l->pos - start;
    l->pos--;
    uint8_t *str = appendStr(l, l->text + start, len);
    if (str == NULL) {
        return false;
    }

    return appendToken(
        l,
        (SlToken){
            .kind = SlToken_Ident,
            .pos = start,
            .as.ident.len =  len,
            .as.ident.value = str
        }
    );
}
