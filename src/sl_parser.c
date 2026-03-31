#include "sl_array.h"
#include "sl_parser.h"
#include "sl_lexer.h"
#include "sl_vartable.h"

#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define S_Fmt "%.*s"
#define S_Arg(name, strs) (int)(name).len, (char *)((strs) + (name).idx)

slArrayType(SlNode, Nodes, nodes)
slArrayImpl(SlNode, Nodes, nodes)

/*
1) Variable creation

When a variable is created it is added to the top frame of `vars`, with a value
of `funcLevel`.

2) Variable access

When accessing a variable the value is updated if the current level is larger
than the old one.

3) Blocks

When a block is opened a new frame is added on top of `vars` and when the block
is closed all variables with a higher funcLevel are added to an array of shared
variables.
*/

typedef struct VarTable {
    struct VarTable *parent;
    SlStrMap *vars;
    uint32_t funcLevel;
    uint16_t sharedCount;
} VarTable;

typedef struct ParserState {
    SlVM *vm;
    const char *path;
    SlTokens tokens;
    Nodes nodes;
    uint32_t idx;
    uint32_t funcLevel;
    SlStrMap *vars; // used when parsing
    VarTable *vt; // used when resolving variable names
} ParserState;

static void setError(const ParserState *p, const char *fmt, ...);
static void setErrorWLine(
    const ParserState *p,
    uint32_t line,
    const char *fmt,
    ...
);

static SlNodeIdx addNode(ParserState *p, SlNode node);
static SlToken token(const ParserState *p);
static SlToken next(ParserState *p);
static bool expect(const ParserState *p, SlTokenKind kind);
static bool expectNext(ParserState *p, SlTokenKind kind);

static bool ensureVars(ParserState *p);
static bool pushVT(ParserState *p, SlStrMap *vars);
static void popVT(ParserState *p);
static bool addVar(ParserState *p, SlStrIdx name);

static SlNodeIdx parseFile(ParserState *p);
static SlNodeIdx parseStatement(ParserState *p);
static SlNodeIdx parseVarDeclr(ParserState *p);
static SlNodeIdx parseFuncDeclr(ParserState *p);
static SlNodeIdx parsePrint(ParserState *p);
static SlNodeIdx parseBlock(ParserState *p, bool keepVars);
static SlNodeIdx parseExpr(ParserState *p);
static SlNodeIdx parseMul(ParserState *p);
static SlNodeIdx parseValue(ParserState *p);

static bool resolveVars(ParserState *p, SlNodeIdx idx);

static void printNode(SlNodeIdx idx, const SlAst *ast, uint32_t indent);
static void printBlock(SlNode node, const SlAst *ast, uint32_t indent);
static void printVarDeclr(SlNode node, const SlAst *ast, uint32_t indent);
static void printBinOp(SlNode node, const SlAst *ast, uint32_t indent);
static void printNumInt(SlNode node, uint32_t indent);
static void printAccess(SlNode node, const SlAst *ast, uint32_t indent);
static void printPrint(SlNode node, const SlAst *ast, uint32_t indent);
static void printRetStmnt(SlNode node, const SlAst *ast, uint32_t indent);
static void printLambda(SlNode node, const SlAst *ast, uint32_t indent);

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
    case SlNode_Access:
        printAccess(node, ast, indent);
        break;
    case SlNode_Print:
        printPrint(node, ast, indent);
        break;
    case SlNode_Lambda:
        printLambda(node, ast, indent);
        break;
    case SlNode_RetStmnt:
        printRetStmnt(node, ast, indent);
            break;
    case SlNode_INVALID:
        assert(false && "invalid node when printing");
    }
}

static void printStrs(const SlAst *ast, const SlStrIdx *strs, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        SlStrIdx str = strs[i];
        printf("%.*s", (int)str.len, (char *)&ast->strs[str.idx]);
        if (i + 1 < count) {
            printf(", ");
        }
    }
}

//noinspection UnreachableCode
static void printBlock(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*sblock [shared=%"PRIu16", funcs=%"PRIu16"]\n",
        indent * INDENT_WIDTH, "",
        node.as.block.sharedCount,
        node.as.block.funcCount
    );
    slMapForeach(node.as.block.vars, SlStrMapBucket, var) {
        printf(
            "%*s- "S_Fmt" @ reg=%"PRIu32", shr=%"PRIi32"\n",
            indent * INDENT_WIDTH, "",
            S_Arg(var->key, ast->strs),
            var->value & 0xff, (int32_t)(var->value >> 16) - 1
        );
    }
    for (uint32_t i = 0; i < node.as.block.nodeCount; i++) {
        printNode(node.as.block.nodes[i], ast, indent + 1);
    }
}

static void printVarDeclr(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*svar "S_Fmt" =\n",
        indent * INDENT_WIDTH, "",
        S_Arg(node.as.varDeclr.name, ast->strs)
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

static void printAccess(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*s"S_Fmt" (access)\n",
        indent * INDENT_WIDTH, "",
        S_Arg(node.as.access, ast->strs)
    );
}

static void printPrint(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*sprint\n", indent * INDENT_WIDTH, "");
    printNode(node.as.print, ast, indent + 1);
}

static void printRetStmnt(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*sreturn\n", indent * INDENT_WIDTH, "");
    printNode(node.as.print, ast, indent + 1);
}

static void printLambda(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*slambda |", indent * INDENT_WIDTH, "");
    printStrs(ast, node.as.lambda.params, node.as.lambda.paramCount);
    printf("|\n");
    printNode(node.as.lambda.body, ast, indent + 1);
}

static void destroyNode(SlNode node) {
    switch (node.kind) {
    case SlNode_Block:
        memFree(&node.as.block.nodes);
        slStrMapClear(node.as.block.vars);
        memFree(node.as.block.vars);
        break;
    case SlNode_Lambda:
        memFree(&node.as.lambda.params);
        break;
    default:
        // Nothing to free
        break;
    }
}

static void destroyNodes(const SlNode *nodes, uint32_t nodeCount) {
    for (uint32_t i = 0; i < nodeCount; i++) {
        destroyNode(nodes[i]);
    }
}

void slDestroyAst(SlAst *ast) {
    destroyNodes(ast->nodes, ast->nodeCount);
    memFree(ast->nodes);
    memFree(ast->strs);
    ast->nodes = NULL;
    ast->nodeCount = 0;
    ast->root = -1;
    ast->strs = NULL;
}

SlAst slParse(SlVM *vm, const SlSource *source) {
    ParserState p = {
        .vm = vm,
        .path = source->path,
        .idx = 0,
        .nodes = { 0 }
    };
    p.tokens = slTokenize(vm, source);
    if (vm->error.occurred) {
        return (SlAst){ .root = -1 };
    }

    SlNodeIdx root = parseFile(&p);

    if (root == -1) {
        destroyNodes(p.nodes.data, p.nodes.len);
        slStrMapClear(p.vars);
        memFree(p.vars);
        return (SlAst){ .root = -1 };
    }

    p.funcLevel = 0;
    p.vars = NULL;
    if (!resolveVars(&p, root)) {
        // now p.vars is always owned by a node, no need to free here
        destroyNodes(p.nodes.data, p.nodes.len);
        return (SlAst){ .root = -1 };
    }

    memFree(p.tokens.tokens);

    SlAst ast = {
        .strs = p.tokens.strs,
        .nodes = p.nodes.data,
        .nodeCount = p.nodes.len,
        .root = root
    };
    char *printAst = getenv("SL_PRINT_AST");
    if (printAst && strcmp(printAst, "true") == 0) {
        printNode(ast.root, &ast, 0);
    }
    return ast;
}

static void setError(const ParserState *p, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[64];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    uint32_t line = token(p).line;
    slSetError(p->vm, "%s:%"PRIu32": %s", p->path, line, buf);
}

static void setErrorWLine(
    const ParserState *p,
    uint32_t line,
    const char *fmt, ...
) {
    va_list args;
    va_start(args, fmt);
    char buf[64];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    slSetError(p->vm, "%s:%"PRIu32": %s", p->path, line, buf);
}

SlNodeIdx addNode(ParserState *p, SlNode node) {
    if (!nodesPush(p->vm, &p->nodes, node)) {
        destroyNode(node);
        return -1;
    }
    return (SlNodeIdx)p->nodes.len - 1;
}

SlToken token(const ParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx];
}

SlToken next(ParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx++];
}

bool expect(const ParserState *p, SlTokenKind kind) {
    if (token(p).kind != kind) {
        setError(
            p,
            "expected %s but found %s instead",
            slTokenKindToStr(kind),
            slTokenKindToStr(token(p).kind)
        );
        return false;
    }
    return true;
}

static bool expectNext(ParserState *p, SlTokenKind kind) {
    if (!expect(p, kind)) {
        return false;
    }
    next(p);
    return true;
}

static bool ensureVars(ParserState *p) {
    if (p->vars) return true;
    p->vars = memAllocZeroed(1, sizeof(*p->vars));
    if (p->vars == NULL) {
        slSetOutOfMemoryError(p->vm);
        return false;
    }
    p->vars->userData = p->tokens.strs;
    return true;
}

SlNodeIdx parseFile(ParserState *p) {
    SlI32Arr nodes = { 0 };
    while (token(p).kind != SlToken_Eof) {
        SlNodeIdx idx = parseStatement(p);
        if (idx == -1) {
            slI32Clear(&nodes);
            return -1;
        }
        if (!slI32Push(p->vm, &nodes, idx)) {
            slI32Clear(&nodes);
            return -1;
        }
    }

    SlNodeIdx body = addNode(p, (SlNode){
        .kind = SlNode_Block,
        .line = 0,
        .as.block = {
            .nodes = nodes.data,
            .nodeCount = nodes.len,
            .vars = p->vars
        }
    });
    p->vars = NULL;
    if (body == -1) {
        slI32Clear(&nodes);
        return -1;
    }

    return addNode(p, (SlNode){
        .kind = SlNode_Lambda,
        .line = 0,
        .as.lambda = {
            .paramCount = 0,
            .body = body
        }
    });
}

SlNodeIdx parseStatement(ParserState *p) {
    switch (token(p).kind) {
    case SlToken_KwVar:
        return parseVarDeclr(p);
    case SlToken_KwPrint:
        return parsePrint(p);
    case SlToken_KwFunc:
        return parseFuncDeclr(p);
    case SlToken_LeftCurly:
        return parseBlock(p, false);
    default:
        setError(
            p,
            "expected a statement, found %s instead",
            slTokenKindToStr(token(p).kind)
        );
        return -1;
    }
}

SlNodeIdx parseVarDeclr(ParserState *p) {
    uint32_t line = next(p).line;
    if (!expect(p, SlToken_Ident)) return -1;
    SlStrIdx name = next(p).as.ident;
    if (!expectNext(p, SlToken_Equals)) return -1;
    SlNodeIdx value = parseExpr(p);
    if (value == -1) return -1;

    if (!expectNext(p, SlToken_Semicolon)) return -1;

    return addNode( p, (SlNode){
        .kind = SlNode_VarDeclr,
        .line = line,
        .as.varDeclr = {
            .name = name,
            .value = value
        }
    });
}

static bool parseFuncParams(ParserState *p, SlStrArr *params) {
    if (!expectNext(p, SlToken_LeftParen)) goto error;

    while (token(p).kind != SlToken_RightParen) {
        if (!expect(p, SlToken_Ident)) goto error;
        SlStrIdx param = token(p).as.ident;

        if (!slStrPush(p->vm, params, param)) goto error;

        next(p);
        if (
            token(p).kind != SlToken_Comma
            && token(p).kind != SlToken_RightParen
        ) {
            setError(
                p,
                "expected ',' or ')' but found %s instead",
                slTokenKindToStr(token(p).kind)
            );
        }

        if (token(p).kind == SlToken_Comma) {
            next(p);
        }
    }
    next(p);
    return true;
error:
    slStrClear(params);
    return false;
}

static SlNodeIdx parseFuncDeclr(ParserState *p) {
    uint32_t line = next(p).line;

    if (!expect(p, SlToken_Ident)) return -1;
    SlStrIdx name = next(p).as.ident;

    if (!addVar(p, name)) return -1;

    SlStrArr params = { 0 };
    if (!parseFuncParams(p, &params)) return -1;

    SlStrMap *prevVars = p->vars;
    p->vars = NULL;

    for (uint32_t i = 0; i < params.len; i++) {
        if (!addVar(p, params.data[i])) goto error;
    }

    SlNodeIdx body = parseBlock(p, true);
    if (body == -1) goto error;
    p->vars = prevVars;
    prevVars = NULL;

    SlNodeIdx value = addNode(p, (SlNode){
        .kind = SlNode_Lambda,
        .line = line,
        .as.lambda = {
            .params = params.data,
            .paramCount = params.len,
            .body = body
        }
    });
    if (value == -1) return -1;

    return addNode(p, (SlNode){
        .kind = SlNode_VarDeclr,
        .line = line,
        .as.varDeclr = {
            .name = name,
            .value = value
        }
    });
error:
    slStrClear(&params);
    slStrMapClear(prevVars);
    memFree(prevVars);
    return -1;
}

static SlNodeIdx parseBlock(ParserState *p, bool keepVars) {
    uint32_t line = next(p).line;
    SlI32Arr nodes = { 0 };
    SlStrMap *prevVars = p->vars;
    if (!keepVars) {
        p->vars = NULL;
    }
    while (token(p).kind != SlToken_RightCurly) {
        SlNodeIdx stmnt = parseStatement(p);
        if (stmnt == -1) goto error;
        if (!slI32Push(p->vm, &nodes, stmnt)) goto error;
    }
    next(p);
    SlStrMap *vars = p->vars;
    p->vars = vars;

    return addNode(p, (SlNode){
        .kind = SlNode_Block,
        .line = line,
        .as.block = {
            .nodes = nodes.data,
            .nodeCount = nodes.len,
            .vars = vars
        }
    });
error:
    slI32Clear(&nodes);
    if (!keepVars) {
        slStrMapClear(prevVars);
        memFree(prevVars);
    }
    return -1;
}

static SlNodeIdx parsePrint(ParserState *p) {
    uint32_t line = next(p).line;
    SlNodeIdx expr = parseExpr(p);
    if (expr == -1) {
        return -1;
    }
    if (!expectNext(p, SlToken_Semicolon)) {
        return -1;
    }
    return addNode(p, (SlNode){
        .kind = SlNode_Print,
        .line =  line,
        .as.print = expr
    });
}

static SlNodeIdx parseExpr(ParserState *p) {
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
        SlNodeIdx binOp = addNode(p, (SlNode){
            .kind = SlNode_BinOp,
            .line = line,
            .as.binOp = {
                .lhs = lhs,
                .rhs = rhs,
                .op = kind == SlToken_Plus ? SlBinOp_Add : SlBinOp_Sub
            }
        });
        if (binOp == -1) {
            return -1;
        }
        lhs = binOp;
    }
    return lhs;
}

static SlNodeIdx parseMul(ParserState *p) {
    SlNodeIdx lhs = parseValue(p);
    if (lhs == -1) {
        return -1;
    }

    for (
        SlTokenKind kind = token(p).kind;
        kind == SlToken_Star || kind == SlToken_FwSlash || kind == SlToken_Perc;
        kind = token(p).kind
    ) {
        uint32_t line = next(p).line;
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

static SlNodeIdx parseValue(ParserState *p) {
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
        SlToken tok = next(p);
        return addNode(
            p,
            (SlNode){
                .kind = SlNode_NumInt,
                .line = tok.line,
                .as.numInt = tok.as.numInt
            }
        );
    }
    case SlToken_Ident: {
        SlToken tok = next(p);
        return addNode(p, (SlNode){
            .kind = SlNode_Access,
            .line = tok.line,
            .as.access = tok.as.ident
        });
    }
    default:
        setError(
            p,
            "expected a value, found %s instead",
            slTokenKindToStr(token(p).kind)
        );
        return -1;
    }
}

static bool pushVT(
    ParserState *p,
    SlStrMap *vars
) {
    VarTable *vt = memAlloc(1, sizeof(*vt));
    if (vt == NULL) {
        slSetOutOfMemoryError(p->vm);
        return false;
    }

    if (vars == NULL) {
        vars = memAllocZeroed(1, sizeof(*vars));
        if (vars == NULL) {
            slSetOutOfMemoryError(p->vm);
            memFree(vt);
            return false;
        }
        vars->userData = p->tokens.strs;
    }

    vt->parent = p->vt;
    vt->funcLevel = p->funcLevel;
    vt->vars = vars;
    vt->sharedCount = 0;

    p->vt = vt;
    p->vars = p->vt->vars;
    return true;
}

static void popVT(ParserState *p) {
    VarTable *vt = p->vt;
    p->vt = p->vt->parent;
    p->vars = p->vt ? p->vt->vars : NULL;
    memFree(vt);
}

static bool addVar(ParserState *p, SlStrIdx name) {
    if (!ensureVars(p)) return false;
    if (slStrMapGet(p->vars, name) != NULL) return true;
    return slStrMapSet(p->vm, p->vars, name, p->vars->len);
}

static bool refVar(const ParserState *p, SlStrIdx name) {
    assert(p->vt != NULL);
    uint32_t funcLevel = p->vt->funcLevel;
    VarTable *vt = p->vt;
    while (vt) {
        uint32_t *var = slStrMapGet(vt->vars, name);
        if (var != NULL) {
            // If the variable is in an outer function and is not already shared
            // then add a share index
            if (vt->funcLevel != funcLevel && *var >> 16 == 0) {
                *var = ++vt->sharedCount << 16 | *var;
            }
            return true;
        }
        vt = vt->parent;
    }
    return false;
}

static bool resolveBlockVars(ParserState *p, SlNode *node);

static bool resolveVars(ParserState *p, SlNodeIdx idx) {
    SlNode *node = nodesAt(&p->nodes, idx);
    switch (node->kind) {
    case SlNode_Block:
        return resolveBlockVars(p, node);
    case SlNode_VarDeclr:
        if (!resolveVars(p, node->as.varDeclr.value)) return false;
        return addVar(p, node->as.varDeclr.name);
    case SlNode_BinOp:
        return resolveVars(p, node->as.binOp.lhs)
            && resolveVars(p, node->as.binOp.rhs);
    case SlNode_NumInt:
        return true;
    case SlNode_Access:
        if (!refVar(p, node->as.access)) {
            setErrorWLine(
                p,
                node->line,
                "unknown variable %"S_Fmt,
                S_Arg(node->as.access, p->tokens.strs)
            );
            return false;
        }
        return true;
    case SlNode_Print:
        return resolveVars(p, node->as.print);
    case SlNode_Lambda: {
        p->funcLevel++;
        if (!resolveVars(p, node->as.lambda.body)) return false;
        SlNode *body = nodesAt(&p->nodes, node->as.lambda.body);
        body->as.block.funcCount -= node->as.lambda.paramCount;
        p->funcLevel--;
        return true;
    }
    case SlNode_RetStmnt:
        return node->as.retStmnt == -1
            ? true
            : resolveVars(p, node->as.retStmnt);
    case SlNode_INVALID:
        assert(false && "invalid node found");
    }
    return false;
}

static bool resolveBlockVars(ParserState *p, SlNode *node) {
    if (!pushVT(p, node->as.block.vars)) {
        return false;
    }
    node->as.block.vars = p->vt->vars;
    // currently args + funcs, when resolving the corresponding lambda the args
    // are removed
    node->as.block.funcCount = p->vt->vars->len;

    for (uint32_t i = 0; i < node->as.block.nodeCount; i++) {
        if (!resolveVars(p, node->as.block.nodes[i])) return false;
    }
    node->as.block.sharedCount = p->vt->sharedCount;
    popVT(p);
    return true;
}
