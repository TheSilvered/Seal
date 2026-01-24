#include "sl_parser.h"
#include "sl_lexer.h"

#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>

slArrayType(SlNode, SlNodes, nodes)
slArrayImpl(SlNode, SlNodes, nodes)

typedef struct SlParserState {
    SlVM *vm;
    const char *path;
    SlTokens tokens;
    uint32_t idx;
    SlNodes nodes;
} SlParserState;

static void setError(SlParserState *p, const char *fmt, ...);
static SlNodeIdx addNode(SlParserState *p, SlNode node);
static SlToken token(SlParserState *p);
static void next(SlParserState *p);
static bool expect(SlParserState *p, SlTokenKind kind);
static bool expectNext(SlParserState *p, SlTokenKind kind);
static SlNodeIdx parseFile(SlParserState *p);
static SlNodeIdx parseStatement(SlParserState *p);
static SlNodeIdx parseVarDeclr(SlParserState *p);
static SlNodeIdx parseExpr(SlParserState *p);
static SlNodeIdx parseMul(SlParserState *p);
static SlNodeIdx parseValue(SlParserState *p);

static void printNode(SlNodeIdx idx, const SlAst *ast, uint32_t indent);
static void printBlock(SlNode node, const SlAst *ast, uint32_t indent);
static void printVarDeclr(SlNode node, const SlAst *ast, uint32_t indent);
static void printBinOp(SlNode node, const SlAst *ast, uint32_t indent);
static void printNumInt(SlNode node, uint32_t indent);

void slPrintAst(const SlAst *ast) {
    printNode(ast->root, ast, 0);
}

#define INDENT_WIDTH 2

static void printNode(SlNodeIdx idx, const SlAst *ast, uint32_t indent) {
    SlNode node = ast->nodes[idx];
    switch (node.kind) {
    case SlNode_Block:
        printBlock(node, ast, indent);
        break;
    case SlNode_VarDeclr:
        printVarDeclr(node, ast, indent);
        break;
    case SlNode_BinOp:
        printBinOp(node, ast, indent);
        break;
    case SlNode_NumInt:
        printNumInt(node, indent);
        break;
    }
}

static void printBlock(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*sblock\n", indent * INDENT_WIDTH, "");
    for (uint32_t i = 0; i < node.as.block.nodeCount; i++) {
        printNode(node.as.block.nodes[i], ast, indent + 1);
    }
}

static void printVarDeclr(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*svar '%.*s'\n",
        indent * INDENT_WIDTH, "",
        node.as.varDeclr.nameLen,
        (char *)&ast->strs[node.as.varDeclr.nameIdx]
    );
    printNode(node.as.varDeclr.value, ast, indent + 1);
}

static void printBinOp(SlNode node, const SlAst *ast, uint32_t indent) {
    const char *op = NULL;
    switch (node.as.binOp.op) {
    case SlBinOp_Add:
        op = "+";
        break;
    case SlBinOp_Sub:
        op = "-";
        break;
    case SlBinOp_Mul:
        op = "*";
        break;
    case SlBinOp_Div:
        op = "/";
        break;
    case SlBinOp_Mod:
        op = "%";
        break;
    case SlBinOp_Pow:
        op = "^";
        break;
    }
    printf("%*s%s\n", indent * INDENT_WIDTH, "", op);
    printNode(node.as.binOp.lhs, ast, indent + 1);
    printNode(node.as.binOp.rhs, ast, indent + 1);
}

static void printNumInt(SlNode node, uint32_t indent) {
    printf("%*s%"PRIi64" (int)\n", indent * INDENT_WIDTH, "", node.as.numInt);
}

SlAst slParse(SlVM *vm, SlSourceHandle sourceHd) {
    SlSource source = slGetSource(vm, sourceHd);
    SlParserState p = {
        .vm = vm,
        .path = source.path,
        .idx = 0,
        .nodes = { 0 },
    };
    p.tokens = slTokenize(vm, sourceHd);
    if (vm->error.occurred) {
        nodesClear(&p.nodes);
        return (SlAst){ .root = -1 };
    }

    SlNodeIdx root = parseFile(&p);
    if (root == -1) {
        nodesClear(&p.nodes);
        return (SlAst){ .root = -1 };
    }
    memFree(p.tokens.tokens);
    return (SlAst){
        .strs = p.tokens.strs,
        .nodes = p.nodes.data,
        .nodeCount = p.nodes.len,
        .root = root
    };
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

bool expect(SlParserState *p, SlTokenKind kind) {
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

static bool expectNext(SlParserState *p, SlTokenKind kind) {
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
            i32Clear(&nodes);
            return -1;
        }
        if (!i32Push(&nodes, idx)) {
            i32Clear(&nodes);
            slSetOutOfMemoryError(p->vm);
            return -1;
        }
    }
    SlNodeIdx node = addNode(
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
    if (node == -1) {
        i32Clear(&nodes);
        return -1;
    } else {
        return node;
    }
}

SlNodeIdx parseStatement(SlParserState *p) {
    switch (token(p).kind) {
    case SlToken_KwVar:
        return parseVarDeclr(p);
    default:
        setError(
            p,
            "expected a value, found %s",
            slTokenKindToStr(token(p).kind)
        );
        return -1;
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
    next(p);

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
    SlNodeIdx lhs = parseMul(p);
    if (lhs == -1) {
        return -1;
    }
    for (
        SlTokenKind kind = token(p).kind;
        kind == SlToken_Plus || kind == SlToken_Hyphen;
        kind = token(p).kind
    ) {
        uint32_t line = token(p).line;
        next(p);
        SlNodeIdx rhs = parseMul(p);
        if (rhs == -1) {
            return -1;
        }
        SlNodeIdx binOp = addNode(
            p,
            (SlNode){
                .kind = SlNode_BinOp,
                .line = line,
                .as.binOp = {
                    .lhs = lhs,
                    .rhs = rhs,
                    .op = kind == SlToken_Plus ? SlBinOp_Add : SlBinOp_Sub
                }
            }
        );
        if (binOp == -1) {
            return -1;
        }
        lhs = binOp;
    }
    return lhs;
}

static SlNodeIdx parseMul(SlParserState *p) {
    SlNodeIdx lhs = parseValue(p);
    if (lhs == -1) {
        return -1;
    }
    
    for (
        SlTokenKind kind = token(p).kind;
        kind == SlToken_Star || kind == SlToken_FwSlash || kind == SlToken_Perc;
        kind = token(p).kind
    ) {
        uint32_t line = token(p).line;
        next(p);
        SlNodeIdx rhs = parseValue(p);
        if (rhs == -1) {
            return -1;
        }
        SlBinOp op;
        switch (kind) {
        case SlToken_Star:
            op = SlBinOp_Mul;
            break;
        case SlToken_FwSlash:
            op = SlBinOp_Div;
            break;
        case SlToken_Perc:
            op = SlBinOp_Mod;
            break;
        default:
            assert("unreachable" && false);
        }
        SlNodeIdx binOp = addNode(
            p,
            (SlNode){
                .kind = SlNode_BinOp,
                .line = line,
                .as.binOp = {
                    .lhs = lhs,
                    .rhs = rhs,
                    .op = op
                }
            }
        );
        if (binOp == -1) {
            return -1;
        }
        lhs = binOp;
    }
    return lhs;
}

static SlNodeIdx parseValue(SlParserState *p) {
    switch (token(p).kind) {
    case SlToken_LeftParen: {
        SlNodeIdx node = parseExpr(p);
        if (node == -1) {
            return -1;
        }
        if (!expectNext(p, SlToken_RightParen)) {
            return -1;
        }
        return node;
    }
    case SlToken_NumInt: {
        SlToken tok = token(p);
        next(p);
        return addNode(
            p, (SlNode){
                .kind = SlNode_NumInt,
                .line = tok.line,
                .as.numInt = tok.as.numInt
            }
        );
    }
    default:
        setError(
            p,
            "expected a value, found %s",
            slTokenKindToStr(token(p).kind)
        );
        return -1;
    }
}
