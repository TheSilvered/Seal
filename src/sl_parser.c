#include "sl_array.h"
#include "sl_parser.h"
#include "sl_lexer.h"
#include "sl_vartable.h"

#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

slArrayType(SlNode, Nodes, nodes)
slArrayImpl(SlNode, Nodes, nodes)

slArrayType(SlUniqueVar, VarArr, varArr)
slArrayImpl(SlUniqueVar, VarArr, varArr)

/*
1) Variable creation

When a variable is created it is added to the top frame of `vars`, with a value
of [funcLevel|ID] where ID is a number incremented in `varCounts` each time a
new variable with the same name is declared.

2) Variable access

When accessing a variable the `funcLevel` part is updated if the current level
is larger than the old one.

3) Blocks

When a block is opened a new frame is added on top of `vars` and when the block
is closed the top frame is moved to the `closedBlocks`.

4) Functions

Before parsing the function, the function itself is added to the top frame of
`vars` and `closedBlocks` is saved to be restored later and cleared. Then the
body is parsed as a regular block, with the arguments already added to the
pushed frame.
After parsing the body all variables in `closedBlocks` that have a higher
`funcLevel` than the current one are saved as shared names.
*/

typedef struct ParserState {
    SlVM *vm;
    const char *path;
    SlTokens tokens;
    Nodes nodes;
    uint32_t idx;
    uint16_t funcLevel;
    SlVarTable *vars;
    SlVarTable *closedBlocks;
    SlStrMap varCounts;
} ParserState;

// Add a new name and get its ID. Return -1 on error.
static int32_t addVar(ParserState *p, SlStrIdx name);
// Get the ID of a referenced name. Return -1 on error.
static int32_t refVar(ParserState *p, SlStrIdx name);
// Add a new VarTable in `p->vars`.
static bool pushVarTable(ParserState *p);
// Move the top table from `p->vars` to `p->closedBlocks`
static void popVarTable(ParserState *p);

static void setError(ParserState *p, const char *fmt, ...);

static SlNodeIdx addNode(ParserState *p, SlNode node);
static SlToken token(ParserState *p);
static SlToken next(ParserState *p);
static bool expect(ParserState *p, SlTokenKind kind);
static bool expectNext(ParserState *p, SlTokenKind kind);

static SlNodeIdx parseFile(ParserState *p);
static SlNodeIdx parseStatement(ParserState *p);
static SlNodeIdx parseVarDeclr(ParserState *p);
static SlNodeIdx parseFuncDeclr(ParserState *p);
static SlNodeIdx parseFuncBody(ParserState *p);
static SlNodeIdx parsePrint(ParserState *p);
static SlNodeIdx parseBlock(ParserState *p);
static SlNodeIdx parseExpr(ParserState *p);
static SlNodeIdx parseMul(ParserState *p);
static SlNodeIdx parseValue(ParserState *p);

static bool getSharedVars(ParserState *p, VarArr *names);

static void printNode(SlNodeIdx idx, const SlAst *ast, uint32_t indent);
static void printBlock(SlNode node, const SlAst *ast, uint32_t indent);
static void printDeclr(
    SlNode node,
    const char *name,
    const SlAst *ast,
    uint32_t indent
);
static void printBinOp(SlNode node, const SlAst *ast, uint32_t indent);
static void printNumInt(SlNode node, uint32_t indent);
static void printAccess(SlNode node, const SlAst *ast, uint32_t indent);
static void printPrint(SlNode node, const SlAst *ast, uint32_t indent);
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
        printDeclr(node, "var", ast, indent);
        break;
    case SlNode_FuncDeclr:
        printDeclr(node, "func", ast, indent);
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
    default:
        assert(false && "unhandled node type");
    }
}

static void printBlock(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*sblock\n", indent * INDENT_WIDTH, "");
    for (uint32_t i = 0; i < node.as.block.nodeCount; i++) {
        printNode(node.as.block.nodes[i], ast, indent + 1);
    }
}

static void printDeclr(
    SlNode node,
    const char *name,
    const SlAst *ast,
    uint32_t indent
) {
    printf(
        "%*s%s %.*s.%"PRIu32" =\n",
        indent * INDENT_WIDTH, "",
        name,
        node.as.varDeclr.name.len,
        (char *)&ast->strs[node.as.varDeclr.name.idx],
        node.as.varDeclr.id
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
        "%*s%.*s.%"PRIu32" (access)\n",
        indent * INDENT_WIDTH, "",
        node.as.access.name.len,
        (char *)&ast->strs[node.as.access.name.idx],
        node.as.access.id
    );
}

static void printPrint(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*sprint\n", indent * INDENT_WIDTH, "");
    printNode(node.as.print, ast, indent + 1);
}

static void printLambda(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*slambda |", indent * INDENT_WIDTH, "");

    for (uint32_t i = 0; i < node.as.lambda.paramCount; i++) {
        SlStrIdx param = node.as.lambda.vars[i].name;
        uint32_t id = node.as.lambda.vars[i].id;
        printf(
            "%.*s.%"PRIu32,
            (int)param.len,
            (char *)&ast->strs[param.idx],
            id
        );
        if (i + 1 < node.as.lambda.paramCount) {
            printf(", ");
        }
    }
    printf("|\n%*s[", indent * INDENT_WIDTH, "");
    for (uint32_t i = 0; i < node.as.lambda.sharedCount; i++) {
        SlStrIdx param = node.as.lambda.vars[i + node.as.lambda.paramCount].name;
        uint32_t id = node.as.lambda.vars[i + node.as.lambda.paramCount].id;
        printf(
            "%.*s.%"PRIu32,
            (int)param.len,
            (char *)&ast->strs[param.idx],
            id
        );
        if (i + 1 < node.as.lambda.paramCount) {
            printf(", ");
        }
    }
    printf("]\n");
    printNode(node.as.lambda.body, ast, indent + 1);
}

static void destroyNode(SlNode node) {
    switch (node.kind) {
    case SlNode_Block:
        memFree(&node.as.block.nodes);
        break;
    case SlNode_Lambda:
        memFree(&node.as.lambda.vars);
        break;
    default:;
        // Do nothing
    }
}

static void destroyNodes(SlNode *nodes, uint32_t nodeCount) {
    for (uint32_t i = 0; i < nodeCount; i++) {
        destroyNode(nodes[i]);
    }
}

void slDestroyAst(SlAst *ast) {
    destroyNodes(ast->nodes, ast->nodeCount);
    memFree(ast->nodes);
    memFree(ast->strs);
}

SlAst slParse(SlVM *vm, SlSource *source) {
    ParserState p = {
        .vm = vm,
        .path = source->path,
        .idx = 0,
        .nodes = { 0 },
        .funcLevel = 0,
        .vars = NULL,
        .closedBlocks = NULL,
        .varCounts = { 0 }
    };
    p.tokens = slTokenize(vm, source);
    if (vm->error.occurred) {
        return (SlAst){ .root = -1 };
    }
    p.varCounts.userData = p.tokens.strs;

    SlNodeIdx root = parseFile(&p);
    slStrMapClear(&p.varCounts);
    while (p.vars != NULL) {
        p.vars = slDelVarTable(p.vars);
    }
    while (p.closedBlocks != NULL) {
        p.closedBlocks = slDelVarTable(p.closedBlocks);
    }

    if (root == -1) {
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

static int32_t addVar(ParserState *p, SlStrIdx name) {
    uint32_t *mapRef;
    // If the variable already exists in the current scope just return the
    // existing ID
    if ((mapRef = slStrMapGet(&p->vars->vars, name)) != NULL) {
        return *mapRef & 0xffff;
    }
    mapRef = slStrMapGet(&p->varCounts, name);
    uint32_t id;
    if (mapRef == NULL) {
        if (!slStrMapSet(&p->varCounts, name, 0)) {
            return false;
        }
        id = 0;
    } else {
        id = ++(*mapRef);
    }

    uint32_t value = (p->funcLevel << 16) | (id & 0xffff);
    if (!slVarTableSet(p->vars, name, value)) {
        slSetOutOfMemoryError(p->vm);
        return -1;
    }
    if (p->vars->vars.len > UINT16_MAX) {
        setError(p, "too many variables, maximum is 65535");
        return -1;
    }
    return (int32_t)id;
}

static int32_t refVar(ParserState *p, SlStrIdx name) {
    uint32_t *var = slVarTableGet(p->vars, name);
    if (var == NULL) {
        setError(
            p,
            "unknown variable %.*s",
            (int)name.len,
            (char *)&p->tokens.strs[name.idx]
        );
        return -1;
    }
    uint32_t id = (*var & 0xffff);
    if (*var >> 16 < p->funcLevel) {
        *var = (p->funcLevel << 16) | id;
    }
    return (int32_t)id;
}

static bool pushVarTable(ParserState *p) {
    SlVarTable *vt = slNewVarTable(p->tokens.strs, p->vars);
    if (vt == NULL) {
        slSetOutOfMemoryError(p->vm);
        return false;
    }
    p->vars = vt;
    return true;
}

static void popVarTable(ParserState *p) {
    SlVarTable *topTable = p->vars;
    p->vars = topTable->parent;
    topTable->parent = p->closedBlocks;
    p->closedBlocks = topTable;
}

static void setError(ParserState *p, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[64];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    uint32_t line = token(p).line;
    slSetError(p->vm, "%s:%"PRIu32": %s", p->path, line, buf);
}

SlNodeIdx addNode(ParserState *p, SlNode node) {
    if (!nodesPush(&p->nodes, node)) {
        slSetOutOfMemoryError(p->vm);
        destroyNode(node);
        return -1;
    }
    return (SlNodeIdx)p->nodes.len - 1;
}

SlToken token(ParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx];
}

SlToken next(ParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx++];
}

bool expect(ParserState *p, SlTokenKind kind) {
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

static bool expectNext(ParserState *p, SlTokenKind kind) {
    if (!expect(p, kind)) {
        return false;
    }
    next(p);
    return true;
}

SlNodeIdx parseFile(ParserState *p) {
    if (!pushVarTable(p)) return -1;
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
    popVarTable(p);
    SlNodeIdx body = addNode(p, (SlNode){
        .kind = SlNode_Block,
        .line = 0,
        .as.block = {
            .nodes = nodes.data,
            .nodeCount = nodes.len
        }
    });
    if (body == -1) return -1;

    VarArr sharedNames = { 0 };
    if (!getSharedVars(p, &sharedNames)) {
        i32Clear(&nodes);
        return -1;
    }

    return addNode(p, (SlNode){
        .kind = SlNode_Lambda,
        .line = 0,
        .as.lambda = {
            .vars = sharedNames.data,
            .paramCount = 0,
            .sharedCount = sharedNames.len,
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
        return parseBlock(p);
    default:
        setError(
            p,
            "expected a value, found %s",
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

    // Add variable only after the declaration to prevent self reference
    int32_t id = addVar(p, name);
    if (id == -1) return -1;

    return addNode( p, (SlNode){
        .kind = SlNode_VarDeclr,
        .line = line,
        .as.varDeclr = {
            .name = name,
            .id = (uint32_t)id,
            .value = value
        }
    });
}

static bool parseFuncParams(ParserState *p, VarArr *names) {
    if (!expectNext(p, SlToken_LeftParen)) return false;

    while (token(p).kind != SlToken_RightParen) {
        if (!expect(p, SlToken_Ident)) return false;
        SlStrIdx param = token(p).as.ident;
        int32_t id = addVar(p, param);
        if (id == -1) {
            varArrClear(names);
            return false;
        }

        if (!varArrPush(names, (SlUniqueVar){ .name = param, .id = id })) {
            slSetOutOfMemoryError(p->vm);
            varArrClear(names);
            return false;
        }
        next(p);
        if (token(p).kind == SlToken_Comma) {
            next(p);
        }
    }
    next(p);
    return true;
}

static bool getSharedVars(ParserState *p, VarArr *names) {
    while (p->closedBlocks != NULL) {
        slMapForeach(&p->closedBlocks->vars, SlStrMapBucket, var) {
            if (var->value >> 16 == p->funcLevel) continue;
            SlUniqueVar uniqueVar = {
                .name = var->key,
                .id = var->value & 0xffff
            };
            if (!varArrPush(names, uniqueVar)) {
                slSetOutOfMemoryError(p->vm);
                return false;
            }
        }
        p->closedBlocks = slDelVarTable(p->closedBlocks);
    }
    return true;
}

static SlNodeIdx parseFuncDeclr(ParserState *p) {
    uint32_t line = next(p).line;

    if (!expect(p, SlToken_Ident)) return -1;
    SlStrIdx name = next(p).as.ident;
    int32_t id = addVar(p, name);
    if (id == -1) return -1;
    if (!pushVarTable(p)) return -1;

    VarArr names = { 0 };
    if (!parseFuncParams(p, &names)) return -1;
    uint16_t paramCount = (uint16_t)names.len;

    p->funcLevel++;
    SlVarTable *prevClosedBlocks = p->closedBlocks;
    p->closedBlocks = NULL;

    SlNodeIdx body = parseFuncBody(p);
    if (body == -1 || !getSharedVars(p, &names)) {
        varArrClear(&names);
        while (prevClosedBlocks != NULL) {
            prevClosedBlocks = slDelVarTable(prevClosedBlocks);
        }
        return -1;
    }
    uint16_t sharedCount = names.len - paramCount;

    p->closedBlocks = prevClosedBlocks;
    p->funcLevel--;

    SlNodeIdx value = addNode(p, (SlNode){
        .kind = SlNode_Lambda,
        .line = line,
        .as.lambda = {
            .vars = names.data,
            .paramCount = paramCount,
            .sharedCount = sharedCount,
            .body = body
        }
    });
    if (value == -1) return -1;

    return addNode(p, (SlNode){
        .kind = SlNode_FuncDeclr,
        .line = line,
        .as.funcDeclr = {
            .name = name,
            .value = value,
            .id = id
        }
    });
}

static SlNodeIdx parseFuncBody(ParserState *p) {
    uint32_t line = token(p).line;
    if (!expectNext(p, SlToken_LeftCurly)) {
        return -1;
    }

    i32Arr nodes = { 0 };
    while (token(p).kind != SlToken_RightCurly) {
        SlNodeIdx stmnt = parseStatement(p);
        if (stmnt == -1) {
            i32Clear(&nodes);
            return -1;
        }
        if (!i32Push(&nodes, stmnt)) {
            i32Clear(&nodes);
            slSetOutOfMemoryError(p->vm);
            return -1;
        }
    }
    next(p);

    // The top variable frame had already been pushed, but it needs to be popped
    popVarTable(p);

    return addNode(p, (SlNode){
        .kind = SlNode_Block,
        .line = line,
        .as.block = {
            .nodes = nodes.data,
            .nodeCount = nodes.len
        }
    });
}

static SlNodeIdx parseBlock(ParserState *p) {
    pushVarTable(p);
    uint32_t line = next(p).line;
    i32Arr nodes = { 0 };
    while (token(p).kind != SlToken_RightCurly) {
        SlNodeIdx stmnt = parseStatement(p);
        if (stmnt == -1) {
            i32Clear(&nodes);
            return -1;
        }
        if (!i32Push(&nodes, stmnt)) {
            i32Clear(&nodes);
            slSetOutOfMemoryError(p->vm);
            return -1;
        }
    }
    next(p);
    popVarTable(p);

    return addNode(p, (SlNode){
        .kind = SlNode_Block,
        .line = line,
        .as.block = {
            .nodes = nodes.data,
            .nodeCount = nodes.len
        }
    });
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
        int32_t id = refVar(p, tok.as.ident);
        if (id == -1) return -1;
        return addNode(p, (SlNode){
            .kind = SlNode_Access,
            .line = tok.line,
            .as.access = {
                .name = tok.as.ident,
                .id = (uint32_t)id
            },
        });
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
