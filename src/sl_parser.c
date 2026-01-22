#include "sl_parser.h"
#include "sl_lexer.h"

#include <assert.h>
#include <stdarg.h>

slArrayType(SlNode, SlNodes, nodes)
slArrayImpl(SlNode, SlNodes, nodes)

typedef struct SlParserState {
    SlVM *vm;
    char *path;
    SlTokens tokens;
    uint32_t idx;
    SlNodes nodes;
} SlParserState;

static void setError(SlParserState *p, const char *fmt, ...);
static SlNodeIdx addNode(SlParserState *p, SlNode node);
static SlToken token(SlParserState *p);
static void next(SlParserState *p);
static bool expect(SlParserState *p, SlNodeKind kind);
static bool expectNext(SlParserState *p, SlNodeKind kind);
static SlNodeIdx parseFile(SlParserState *p);
static SlNodeIdx parseStatement(SlParserState *p);
static SlNodeIdx parseVarDeclr(SlParserState *p);
static SlNodeIdx parseExpr(SlParserState *p);

SlAst slParse(SlVM *vm, SlSourceHandle sourceHd) {
    SlParserState p = {
        .vm = vm,
        .idx = 0,
        .nodes = { 0 }
    };
    p.tokens = slTokenize(vm, sourceHd);
    if (vm->error.occurred) {
        return (SlAst){ .strs = NULL };
    }

    SlNodeIdx root = parseFile(&p);
}

static void setError(SlParserState *p, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[64];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    uint32_t line = token(p).line;
    slSetError(p->vm, "%s:%"PRIu32": %s", p->path, line, buf);
}

SlNodeIdx addNode(SlParserState *p, SlNode node) {
    if (!nodesPush(&p->nodes, node)) {
        slSetOutOfMemoryError(p->vm);
        return -1;
    }
    return (SlNodeIdx)p->nodes.len - 1;
}

SlToken token(SlParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx];
}

void next(SlParserState *p) {
    p->idx++;
}

bool expect(SlParserState *p, SlNodeKind kind) {
    if (token(p).kind != kind) {
        setError(
            p,
            "expected %s but found %s",
            slTokenKindToStr(kind),
            slTokenKindToStr(token(p).kind)
        );
        return false;
    }
    return true;
}

static bool expectNext(SlParserState *p, SlNodeKind kind) {
    if (!expect(p, kind)) {
        return false;
    }
    next(p);
    return true;
}

SlNodeIdx parseFile(SlParserState *p) {
    i32Arr nodes = { 0 };
    while (token(p).kind != SlToken_Eof) {
        SlNodeIdx idx = parseStatement(p);
        if (idx == -1) {
            nodesClear(&nodes);
            return -1;
        }
        i32Push(&nodes, idx);
    }
    addNode(
        p,
        (SlNode){
            .kind = SlNode_Block,
            .line = 0,
            .as.block = {
                .nodes = nodes.data,
                .nodeCount = nodes.len
            }
        }
    );
}

SlNodeIdx parseStatement(SlParserState *p) {
    switch (token(p).kind) {
    case SlToken_KwVar:
        return parseVarDeclr(p);
    }
}

SlNodeIdx parseVarDeclr(SlParserState *p) {
    uint32_t line = token(p).line;
    // consume 'var'
    next(p);
    if (!expect(p, SlToken_Ident)) {
        return -1;
    }

    uint32_t nameIdx = token(p).as.ident.strIdx;
    uint32_t nameLen = token(p).as.ident.len;

    if (!expectNext(p, SlToken_Equals)) {
        return -1;
    }

    SlNodeIdx value = parseExpr(p);
    if (value == -1) {
        return -1;
    }

    if (!expectNext(p, SlToken_Semicolon)) {
        return -1;
    }

    return addNode(
        p,
        (SlNode){
            .kind = SlNode_VarDeclr,
            .line = line,
            .as.varDeclr = {
                .nameIdx = nameIdx,
                .nameLen = nameLen,
                .value = value
            }
        }
    );
}

static SlNodeIdx parseExpr(SlParserState *p) {
    setError(p, "TODO");
    return -1;
}
