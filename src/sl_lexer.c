#include "sl_lexer.h"
#include "sl_array.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

slArrayType(SlToken, Tokens, tokens)
slArrayImpl(SlToken, Tokens, tokens)

typedef struct LexerState {
    SlVM *vm;

    const char *path;
    uint8_t *text;
    uint32_t len;

    uint8_t *strs;
    uint32_t strsLen;
    uint32_t strsCap;

    Tokens tokens;

    uint32_t pos;
    uint32_t line;
} LexerState;

static const struct {
    char *str;
    SlTokenKind kind;
} keywords[] = {
    { "var", SlToken_KwVar },
    { "func", SlToken_KwFunc }
};
static const size_t keywordsLen = sizeof(keywords) / sizeof(*keywords);

static void setError(LexerState *l, const char *fmt, ...);
static bool appendToken(LexerState *l, SlToken token);
static bool appendSimpleToken(LexerState *l, SlTokenKind kind);
static uint32_t appendStr(LexerState *l, uint8_t *str, uint32_t len);
static bool appendNumber(LexerState *l);
static bool appendIdent(LexerState *l);

const char *slTokenKindToStr(SlTokenKind kind) {
    switch (kind) {
    case SlToken_Ident:
        return "<identifier>";
    case SlToken_NumInt:
        return "<number>";
    case SlToken_Plus:
        return "'+'";
    case SlToken_Star:
        return "'*'";
    case SlToken_Hyphen:
        return "'-'";
    case SlToken_FwSlash:
        return "'/'";
    case SlToken_Perc:
        return "'%'";
    case SlToken_Comma:
        return "','";
    case SlToken_Colon:
        return "':'";
    case SlToken_Semicolon:
        return "';'";
    case SlToken_LeftParen:
        return "'('";
    case SlToken_RightParen:
        return "')'";
    case SlToken_LeftSquare:
        return "'['";
    case SlToken_RightSquare:
        return "']'";
    case SlToken_LeftCurly:
        return "'{'";
    case SlToken_RightCurly:
        return "'}'";
    case SlToken_Equals:
        return "'='";
    case SlToken_KwVar:
        return "'var'";
    case SlToken_KwFunc:
        return "'func'";
    case SlToken_Eof:
        return "<end of file>";
    default:
        assert("unreachable" && false);
        return "";
    }
}

SlTokens slTokenize(SlVM *vm, SlSource *source) {
    LexerState l = {
        .vm = vm,
        .path = source->path,
        .text = source->text,
        .len = source->textLen,
        .strs = NULL,
        .strsLen = 0,
        .strsCap = 0,
        .tokens = { 0 },
        .pos = 0,
        .line = 1
    };

    for (l.pos = 0; l.pos < l.len; l.pos++) {
        uint8_t ch = l.text[l.pos];
        bool success = true;
        if (isspace(ch)) {
            if (ch == '\n') {
                l.line++;
            }
            continue;
        } else if (ch == '+') {
            success = appendSimpleToken(&l, SlToken_Plus);
        } else if (ch == '*') {
            success = appendSimpleToken(&l, SlToken_Star);
        } else if (ch == '-') {
            success = appendSimpleToken(&l, SlToken_Hyphen);
        } else if (ch == '/') {
            success = appendSimpleToken(&l, SlToken_FwSlash);
        } else if (ch == '%') {
            success = appendSimpleToken(&l, SlToken_Perc);
        } else if (ch == ',') {
            success = appendSimpleToken(&l, SlToken_Comma);
        } else if (ch == ':') {
            success = appendSimpleToken(&l, SlToken_Colon);
        } else if (ch == ';') {
            success = appendSimpleToken(&l, SlToken_Semicolon);
        } else if (ch == '(') {
            success = appendSimpleToken(&l, SlToken_LeftParen);
        } else if (ch == ')') {
            success = appendSimpleToken(&l, SlToken_RightParen);
        } else if (ch == '[') {
            success = appendSimpleToken(&l, SlToken_LeftSquare);
        } else if (ch == ']') {
            success = appendSimpleToken(&l, SlToken_RightSquare);
        } else if (ch == '{') {
            success = appendSimpleToken(&l, SlToken_LeftCurly);
        } else if (ch == '}') {
            success = appendSimpleToken(&l, SlToken_RightCurly);
        } else if (ch == '=') {
            success = appendSimpleToken(&l, SlToken_Equals);
        } else if (isdigit(ch)) {
            success = appendNumber(&l);
        } else if (isalpha(ch) || ch == '_') {
            success = appendIdent(&l);
        } else {
            if (isprint(ch)) {
                setError(&l, "invalid character %c", ch);
            } else {
                setError(&l, "invalid byte 0x%02x", ch);
            }
            success = false;
        }

        if (!success) {
            tokensClear(&l.tokens);
            memFree(l.strs);
            return (SlTokens) {
                .strs = NULL,
                .tokens = NULL,
                .tokenCount = 0
            };
        }
    }

    appendSimpleToken(&l, SlToken_Eof);

    return (SlTokens) {
        .strs = l.strs,
        .tokens = l.tokens.data,
        .tokenCount = l.tokens.len
    };
}

static void setError(LexerState *l, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[64];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    slSetError(l->vm, "%s:%"PRIu32": %s", l->path, l->line, buf);
}

static bool appendToken(LexerState *l, SlToken token) {
    if (!tokensPush(&l->tokens, token)) {
        slSetOutOfMemoryError(l->vm);
        return false;
    }
    return true;
}

static bool appendSimpleToken(LexerState *l, SlTokenKind kind) {
    return appendToken(l, (SlToken){ .kind = kind, .line = l->line });
}

static uint32_t appendStr(LexerState *l, uint8_t *str, uint32_t len) {
    for (uint32_t i = 0; i + len <= l->strsLen; i++) {
        for (uint32_t j = 0; j < len; j++) {
            if (l->strs[i + j] != str[j]) {
                goto outerContinue;
            }
        }
        return i;
outerContinue:;
    }

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

static bool appendNumber(LexerState *l) {
    int64_t n = 0;
    uint32_t pos = l->pos;
    while (l->pos < l->len && isdigit(l->text[l->pos])) {
        n = n*10 + (l->text[l->pos] - '0');
        l->pos++;
    }
    l->pos--;
    return appendToken(
        l,
        (SlToken){ .kind = SlToken_NumInt, .line = l->line, .as.numInt = n }
    );
}

static bool appendIdent(LexerState *l) {
    uint32_t start = l->pos;
    uint32_t line = l->line;

    while (
        l->pos < l->len
        && (isalnum(l->text[l->pos]) || l->text[l->pos] == '_')
    ) {
        l->pos++;
    }

    uint32_t len = l->pos - start;
    l->pos--;
    
    const char *identStr = (const char *)(l->text + start);
    for (size_t i = 0; i < keywordsLen; i++) {
        if (strncmp(identStr, keywords[i].str, len) == 0) {
            return appendToken(
                l,
                (SlToken){ .kind = keywords[i].kind, .line = line }
            );
        }
    }

    uint32_t strIdx = appendStr(l, l->text + start, len);
    if (strIdx == 0 && l->vm->error.occurred) {
        return false;
    }

    return appendToken(
        l,
        (SlToken){
            .kind = SlToken_Ident,
            .line = line,
            .as.ident.len =  len,
            .as.ident.idx = strIdx
        }
    );
}
