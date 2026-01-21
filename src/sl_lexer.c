#include "sl_lexer.h"
#include "sl_array.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

SL_ARRAY_TYPE(SlToken, SlTokenList);
SL_ARRAY_SRC(SlToken, SlTokenList, tokens);

typedef struct SlLexerState {
    SlVM *vm;

    uint8_t *text;
    uint32_t len;

    uint8_t *strs;
    uint32_t strsLen;
    uint32_t strsCap;

    SlTokenList tokens;

    uint32_t pos;
} SlLexerState;

static const struct {
    char *str;
    SlTokenKind kind;
} keywords[] = {
    { "var", SlToken_KwVar },
    { "func", SlToken_KwFunc }
};
static const size_t keywordsLen = sizeof(keywords) / sizeof(*keywords);

static bool appendToken(SlLexerState *l, SlToken token);
static uint32_t appendStr(SlLexerState *l, uint8_t *str, uint32_t len);
static bool appendNumber(SlLexerState *l);
static bool appendIdent(SlLexerState *l);

SlTokens slTokenize(SlVM *vm, const uint8_t *text, uint32_t len) {
    SlLexerState l = {
        .vm = vm,
        .text = text,
        .len = len,
        .strs = NULL,
        .strsLen = 0,
        .strsCap = 0,
        .tokens = { 0 },
        .pos = 0
    };

    for (l.pos = 0; l.pos < len; l.pos++) {
        uint8_t ch = text[l.pos];
        bool success = true;
        if (isspace(ch)) {
            continue;
        } else if (ch == '+') {
            success = appendToken(&l, (SlToken){ .kind = SlToken_Plus });
        } else if (ch == ',') {
            success = appendToken(&l, (SlToken){ .kind = SlToken_Comma });
        } else if (ch == ';') {
            success = appendToken(&l, (SlToken){ .kind = SlToken_Semicolon });
        } else if (ch == '(') {
            success = appendToken(&l, (SlToken){ .kind = SlToken_LeftParen });
        } else if (ch == ')') {
            success = appendToken(&l, (SlToken){ .kind = SlToken_RightParen });
        } else if (ch == '=') {
            success = appendToken(&l, (SlToken){ .kind = SlToken_Equals });
        } else if (isdigit(ch)) {
            success = appendNumber(&l);
        } else if (isalpha(ch) || ch == '_') {
            success = appendIdent(&l);
        } else {
            if (isprint(ch)) {
                slSetError(vm, "invalid character %c", ch);
            } else {
                slSetError(vm, "invalid byte 0x%02x", ch);
            }
            success = false;
        }

        if (!success) {
            tokenClear(l.tokens);
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
        .tokens = l.tokens.data,
        .tokenCount = l.tokens.len
    };
}

static bool appendToken(SlLexerState *l, SlToken token) {
    if (!tokensPush(&l->tokens, token)) {
        slSetOutOfMemoryError(l->vm);
        return false;
    }
    return true;
}

static uint32_t appendStr(SlLexerState *l, uint8_t *str, uint32_t len) {
    if (l->strsLen + len > l->strsCap) {
        uint32_t newCap = (len + l->strsLen) * 2;
        uint8_t *newStrs = memChange(l->strs, newCap, sizeof(*l->strs));
        if (newStrs == NULL) {
            slSetOutOfMemoryError(l->vm);
            return 0;
        }
        l->strs = newStrs;
        l->strsCap = newCap;
    }
    uint32_t outIdx = l->strsLen;
    memcpy(l->strs + l->strsLen, str, len);
    l->strsLen += len;
    return outIdx;
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
        (SlToken){ .kind = SlToken_NumInt, .pos = pos, .as.numInt = n }
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
    uint32_t strIdx = appendStr(l, l->text + start, len);
    if (strIdx == 0 && l->vm->error.occurred) {
        return false;
    }

    const char *identStr = (const char *)(l->text + start);
    for (size_t i = 0; i < keywordsLen; i++) {
        if (strncmp(identStr, keywords[i].str, len) == 0) {
            return appendToken(l, (SlToken){ .kind = keywords[i].kind });
        }
    }

    return appendToken(
        l,
        (SlToken){
            .kind = SlToken_Ident,
            .pos = start,
            .as.ident.len =  len,
            .as.ident.strIdx = strIdx
        }
    );
}
