#include "sl_array.h"
#include "sl_parser.h"
#include "sl_lexer.h"

#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define S_Fmt "%.*s"
#define S_Arg(name, strs) (int)(name).len, (char *)((strs) + (name).idx)

slArrayType(SlNode, Nodes, nodes)
slArrayImpl(SlNode, Nodes, nodes)

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
SlNodeIdx addLambda(
    ParserState *p,
    uint32_t line,
    SlStrMap *params,
    SlNodeIdx innerBody
);

// Get the current token
static SlToken token(const ParserState *p);
// Get the current token and advance to the next
static SlToken next(ParserState *p);
// Get the token n tokens ahead, if n is too big just return EOF
static SlToken ahead(ParserState *p, uint32_t n);
// Get the current token and raise an error if it is not of kind `kind`
static bool expect(const ParserState *p, SlTokenKind kind);
// Same as `expect` but advance after the check
static bool expectNext(ParserState *p, SlTokenKind kind);

static bool addVar(const ParserState *p, SlStrIdx name);

static SlNodeIdx parseFile(ParserState *p);
static SlNodeIdx parseStatement(ParserState *p);
static SlNodeIdx parseVarDeclr(ParserState *p);
static SlNodeIdx parseFuncDeclr(ParserState *p);
static SlNodeIdx parsePrint(ParserState *p);
static SlNodeIdx parseBlock(ParserState *p);
static SlNodeIdx parseRetStmnt(ParserState *p);
static SlNodeIdx parseIfStmnt(ParserState *p);
static SlNodeIdx parseWhileLoop(ParserState *p);
static SlNodeIdx parseAssign(ParserState *p);
static SlNodeIdx parseExpr(ParserState *p);
static SlNodeIdx parseAdd(ParserState *p);
static SlNodeIdx parseMul(ParserState *p);
static SlNodeIdx parseValue(ParserState *p);

static bool resolveVars(ParserState *p, SlNodeIdx idx);

static void printNode(SlNodeIdx idx, const SlAst *ast, uint32_t indent);
static void printBlock(SlNode node, const SlAst *ast, uint32_t indent);
static void printVarDeclr(SlNode node, const SlAst *ast, uint32_t indent);
static void printIfStmnt(SlNode node, const SlAst *ast, uint32_t indent);
static void printWhileLoop(SlNode node, const SlAst *ast, uint32_t indent);
static void printBinOp(SlNode node, const SlAst *ast, uint32_t indent);
static void printNumInt(SlNode node, uint32_t indent);
static void printBoolLit(SlNode node, uint32_t indent);
static void printNullLit(uint32_t indent);
static void printAccess(SlNode node, const SlAst *ast, uint32_t indent);
static void printAssign(SlNode node, const SlAst *ast, uint32_t indent);
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
    case SlNode_IfStmnt:
        printIfStmnt(node, ast, indent);
        break;
    case SlNode_WhileLoop:
        printWhileLoop(node, ast, indent);
        break;
    case SlNode_Print:
        printPrint(node, ast, indent);
        break;
    case SlNode_RetStmnt:
        printRetStmnt(node, ast, indent);
        break;
    case SlNode_BinOp:
        printBinOp(node, ast, indent);
        break;
    case SlNode_NumInt:
        printNumInt(node, indent);
        break;
    case SlNode_BoolLit:
        printBoolLit(node, indent);
        break;
    case SlNode_NullLit:
        printNullLit(indent);
        break;
    case SlNode_Access:
        printAccess(node, ast, indent);
        break;
    case SlNode_Assign:
        printAssign(node, ast, indent);
        break;
    case SlNode_Lambda:
        printLambda(node, ast, indent);
        break;
    case SlNode_INVALID:
        assert(false && "invalid node when printing");
    }
}

static void printBlock(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*sblock [shared=%"PRIu16", funcs=%"PRIu16"]\n",
        indent * INDENT_WIDTH, "",
        node.as.block.sharedCount,
        node.as.block.funcCount
    );
    slMapForeach(node.as.block.vars, SlStrMapBucket, var, i) {
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

static void printIfStmnt(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*sif:\n", indent * INDENT_WIDTH, "");
    printNode(node.as.ifStmnt.condition, ast, indent + 1);
    printf("%*s(ifTrue):\n", indent * INDENT_WIDTH, "");
    printNode(node.as.ifStmnt.ifTrue, ast, indent + 1);

    if (node.as.ifStmnt.ifFalse == -1) return;

    printf("%*s(ifFalse):\n", indent * INDENT_WIDTH, "");
    printNode(node.as.ifStmnt.ifFalse, ast, indent + 1);
}

static void printWhileLoop(SlNode node, const SlAst *ast, uint32_t indent) {
    printf("%*swhile:\n", indent * INDENT_WIDTH, "");
    printNode(node.as.whileLoop.condition, ast, indent + 1);
    printf("%*s(body):\n", indent * INDENT_WIDTH, "");
    printNode(node.as.whileLoop.body, ast, indent + 1);
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
    case SlBinOp_Lt:
        op = "<";
        break;
    case SlBinOp_Le:
        op = "<=";
        break;
    case SlBinOp_Gt:
        op = ">";
        break;
    case SlBinOp_Ge:
        op = ">=";
        break;
    case SlBinOp_Eq:
        op = "==";
        break;
    case SlBinOp_Ne:
        op = "!=";
        break;
    }
    printf("%*s%s\n", indent * INDENT_WIDTH, "", op);
    printNode(node.as.binOp.lhs, ast, indent + 1);
    printNode(node.as.binOp.rhs, ast, indent + 1);
}

static void printNumInt(SlNode node, uint32_t indent) {
    printf("%*s%"PRIi64" (int)\n", indent * INDENT_WIDTH, "", node.as.numInt);
}

static void printBoolLit(SlNode node, uint32_t indent) {
    printf(
        "%*s%s (bool)\n",
        indent * INDENT_WIDTH, "",
        node.as.boolLit ? "true" : "false"
    );
}

static void printNullLit(uint32_t indent) {
    printf("%*snull (null)\n", indent * INDENT_WIDTH, "");
}

static void printAccess(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*s"S_Fmt" (%s access)\n",
        indent * INDENT_WIDTH, "",
        S_Arg(node.as.access.name, ast->strs),
        node.as.access.local ? "local" : "nonlocal"
    );
}

static void printAssign(SlNode node, const SlAst *ast, uint32_t indent) {
    printf(
        "%*s"S_Fmt" (%s) =\n",
        indent * INDENT_WIDTH, "",
        S_Arg(node.as.assign.name, ast->strs),
        node.as.assign.local ? "local" : "nonlocal"
    );
    printNode(node.as.assign.value, ast, indent + 1);
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
    printf("%*slambda\n", indent * INDENT_WIDTH, "");
    printNode(node.as.lambda.body, ast, indent + 1);
}

static void destroyNode(SlNode node) {
    switch (node.kind) {
    case SlNode_Block:
        memFree(node.as.block.nodes);
        slStrMapClear(node.as.block.vars);
        memFree(node.as.block.vars);
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

SlNodeIdx addLambda(
    ParserState *p,
    uint32_t line,
    SlStrMap *params,
    SlNodeIdx innerBody
) {
    SlNodeIdx outerBody = innerBody;
    if (params != NULL && params->len != 0) {
        SlNodeIdx *outerBodyNodes = memAlloc(1, sizeof(*outerBodyNodes));
        if (outerBodyNodes == NULL) {
            slSetOutOfMemoryError(p->vm);
            goto error;
        }
        *outerBodyNodes = innerBody;

        outerBody = addNode(p, (SlNode){
            .kind = SlNode_Block,
            .line = line,
            .as.block = {
                .nodes = outerBodyNodes,
                .nodeCount = 1,
                .vars = params,
                .sharedCount = 0,
                .funcCount = 0
            }
        });
        if (outerBody == -1) goto error;
    }

    return addNode(p, (SlNode){
        .kind = SlNode_Lambda,
        .line = line,
        .as.lambda = {
            .paramCount = params ? params->len : 0,
            .body = outerBody
        }
    });

error:
    slStrMapClear(params);
    memFree(params);
    return -1;
}

SlToken token(const ParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx];
}

SlToken next(ParserState *p) {
    assert(p->idx < p->tokens.tokenCount);
    return p->tokens.tokens[p->idx++];
}

static SlToken ahead(ParserState *p, uint32_t n) {
    uint32_t idx = p->idx + n;
    if (idx >= p->tokens.tokenCount) {
        idx = p->tokens.tokenCount - 1;
    }

    return p->tokens.tokens[idx];
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

SlNodeIdx parseFile(ParserState *p) {
    SlI32Arr nodes = { 0 };
    p->vars = memAllocZeroed(1, sizeof(*p->vars));
    if (p->vars == NULL) {
        slSetOutOfMemoryError(p->vm);
        return -1;
    }
    p->vars->userData = p->tokens.strs;

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

    return addLambda(p, 0, NULL, body);
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
    case SlToken_KwReturn:
        return parseRetStmnt(p);
    case SlToken_KwIf:
        return parseIfStmnt(p);
    case SlToken_KwWhile:
        return parseWhileLoop(p);
    case SlToken_Ident: {
        SlNodeIdx idx = parseAssign(p);
        if (idx == -1 || !expectNext(p, SlToken_Semicolon)) return -1;
        return idx;
    }
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

static SlStrMap *parseFuncParams(ParserState *p) {
    SlStrMap *params = memAllocZeroed(1, sizeof(*params));
    if (params == NULL) {
        slSetOutOfMemoryError(p->vm);
        return NULL;
    }
    params->userData = p->tokens.strs;

    if (!expectNext(p, SlToken_LeftParen)) goto error;

    while (token(p).kind != SlToken_RightParen) {
        if (!expect(p, SlToken_Ident)) goto error;
        SlStrIdx param = token(p).as.ident;

        if (!slStrMapSet(p->vm, params, param, params->len)) goto error;

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
            goto error;
        }

        if (token(p).kind == SlToken_Comma) {
            next(p);
        }
    }
    next(p);
    return params;
error:
    slStrMapClear(params);
    memFree(params);
    return false;
}

static SlNodeIdx parseFuncDeclr(ParserState *p) {
    uint32_t line = next(p).line;

    if (!expect(p, SlToken_Ident)) return -1;
    SlStrIdx name = next(p).as.ident;

    if (!addVar(p, name)) return -1;

    SlStrMap *params = parseFuncParams(p);
    if (params == NULL) {
        return false;
    }

    p->funcLevel++;
    SlNodeIdx body = parseBlock(p);
    if (body == -1) goto error;
    p->funcLevel--;

    SlNodeIdx lambda = addLambda(p, line, params, body);
    if (lambda == -1) return -1;

    return addNode(p, (SlNode){
        .kind = SlNode_VarDeclr,
        .line = line,
        .as.varDeclr = {
            .name = name,
            .value = lambda
        }
    });
error:
    slStrMapClear(params);
    memFree(params);
    return -1;
}

static SlNodeIdx parseBlock(ParserState *p) {
    uint32_t line = next(p).line;
    SlI32Arr nodes = { 0 };
    SlStrMap *prevVars = p->vars;
    p->vars = memAllocZeroed(1, sizeof(*p->vars));
    if (p->vars == NULL) {
        slSetOutOfMemoryError(p->vm);
        goto error;
    }
    p->vars->userData = p->tokens.strs;
    while (token(p).kind != SlToken_RightCurly) {
        SlNodeIdx stmnt = parseStatement(p);
        if (stmnt == -1) goto error;
        if (!slI32Push(p->vm, &nodes, stmnt)) goto error;
    }
    next(p);
    SlStrMap *vars = p->vars;
    p->vars = prevVars;

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
    slStrMapClear(prevVars);
    memFree(prevVars);
    return -1;
}

static SlNodeIdx parseRetStmnt(ParserState *p) {
    if (p->funcLevel == 0) {
        setError(p, "'return' statement outside of a function");
        return -1;
    }

    uint32_t line = next(p).line;
    SlNodeIdx expr = -1;
    if (token(p).kind != SlToken_Semicolon) {
        expr = parseExpr(p);
        if (expr == -1) return -1;
    }
    next(p);

    return addNode(p, (SlNode){
        .kind = SlNode_RetStmnt,
        .line = line,
        .as.retStmnt = expr
    });
}

static SlNodeIdx parseIfStmnt(ParserState *p) {
    uint32_t line = next(p).line;

    SlNodeIdx condition = parseExpr(p);
    if (condition == -1) return -1;
    if (!expect(p, SlToken_LeftCurly)) return -1;
    SlNodeIdx ifTrue = parseBlock(p);
    if (ifTrue == -1) return -1;
    SlNodeIdx ifFalse = -1;

    if (token(p).kind != SlToken_KwElse) goto end;
    next(p);

    if (token(p).kind == SlToken_LeftCurly) {
        ifFalse = parseBlock(p);
        if (ifFalse == -1) return -1;
    } else if (token(p).kind == SlToken_KwIf) {
        ifFalse = parseIfStmnt(p);
        if (ifFalse == -1) return -1;
    } else {
        setError(
            p,
            "expected '{' or 'if' but found %s",
            slTokenKindToStr(token(p).kind)
        );
        return -1;
    }

end:
    return addNode(p, (SlNode){
        .kind = SlNode_IfStmnt,
        .line = line,
        .as.ifStmnt = {
            .condition = condition,
            .ifTrue = ifTrue,
            .ifFalse = ifFalse
        }
    });
}

static SlNodeIdx parseWhileLoop(ParserState *p) {
    uint32_t line = next(p).line;
    SlNodeIdx condition = parseExpr(p);
    if (condition == -1) return -1;
    SlNodeIdx body = parseBlock(p);
    if (body == -1) return -1;

    return addNode(p, (SlNode){
        .kind = SlNode_WhileLoop,
        .line = line,
        .as.whileLoop = {
            .condition = condition,
            .body = body
        }
    });
}

static SlNodeIdx parseAssign(ParserState *p) {
    SlStrIdx name = token(p).as.ident;
    uint32_t line = next(p).line;

    if (!expectNext(p, SlToken_Equals)) return -1;

    SlNodeIdx value = parseExpr(p);
    if (value == -1) return -1;

    return addNode(p, (SlNode){
        .kind = SlNode_Assign,
        .line = line,
        .as.assign = {
            .name = name,
            .value = value,
            .local = true // actually set in resolveVars
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
    if (token(p).kind == SlToken_Ident && ahead(p, 1).kind == SlToken_Equals) {
        return parseAssign(p);
    }

    SlNodeIdx lhs = parseAdd(p);
    if (lhs == -1) {
        return -1;
    }
    for (
        SlTokenKind kind = token(p).kind;
        kind == SlToken_DoubleEquals || kind == SlToken_BangEquals ||
        kind == SlToken_GreaterThan || kind == SlToken_GreaterThanEquals ||
        kind == SlToken_LessThan || kind == SlToken_LessThanEquals;
        kind = token(p).kind
    ) {
        uint32_t line = token(p).line;
        next(p);
        SlNodeIdx rhs = parseAdd(p);
        if (rhs == -1) {
            return -1;
        }
        SlBinOp op = SlBinOp_Add;
        switch (kind) {
        case SlToken_DoubleEquals:      op = SlBinOp_Eq; break;
        case SlToken_BangEquals:        op = SlBinOp_Ne; break;
        case SlToken_GreaterThan:       op = SlBinOp_Gt; break;
        case SlToken_GreaterThanEquals: op = SlBinOp_Ge; break;
        case SlToken_LessThan:          op = SlBinOp_Lt; break;
        case SlToken_LessThanEquals:    op = SlBinOp_Le; break;
        default:
            assert(false && "unreachable");
        }

        SlNodeIdx binOp = addNode(p, (SlNode){
            .kind = SlNode_BinOp,
            .line = line,
            .as.binOp = {
                .lhs = lhs,
                .rhs = rhs,
                .op = op
            }
        });
        if (binOp == -1) {
            return -1;
        }
        lhs = binOp;
    }
    return lhs;
}

static SlNodeIdx parseAdd(ParserState *p) {
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
        if (rhs == -1) return -1;
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
        SlBinOp op = SlBinOp_Mul;
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
            .as.access = {
                .name = tok.as.ident,
                .local = true // actually set in resolveVars
            }
        });
    }
    case SlToken_KwTrue:
        return addNode(p, (SlNode){
            .kind = SlNode_BoolLit,
            .line = next(p).line,
            .as.boolLit = true
        });
    case SlToken_KwFalse:
        return addNode(p, (SlNode){
            .kind = SlNode_BoolLit,
            .line = next(p).line,
            .as.boolLit = false
        });
    case SlToken_KwNull:
        return addNode(p, (SlNode){
            .kind = SlNode_NullLit,
            .line = next(p).line
        });
    default:
        setError(
            p,
            "expected a value, found %s instead",
            slTokenKindToStr(token(p).kind)
        );
        return -1;
    }
}

static bool addVar(const ParserState *p, SlStrIdx name) {
    if (slStrMapGet(p->vars, name) != NULL) return true;
    return slStrMapSet(p->vm, p->vars, name, p->vars->len);
}

typedef enum RefKind {
    Ref_failed,
    Ref_local,
    Ref_nonlocal
} RefKind;

static RefKind refVar(const ParserState *p, SlStrIdx name) {
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
                return Ref_nonlocal;
            }
            return Ref_local;
        }
        vt = vt->parent;
    }
    return Ref_failed;
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
    case SlNode_IfStmnt:
        if (!resolveVars(p, node->as.ifStmnt.condition)) return false;
        if (!resolveVars(p, node->as.ifStmnt.ifTrue)) return false;
        if (
            node->as.ifStmnt.ifFalse != -1
            && !resolveVars(p, node->as.ifStmnt.condition)
        ){
            return false;
        }
        return true;
    case SlNode_WhileLoop:
        return resolveVars(p, node->as.whileLoop.condition)
            && resolveVars(p, node->as.whileLoop.body);
    case SlNode_BinOp:
        return resolveVars(p, node->as.binOp.lhs)
            && resolveVars(p, node->as.binOp.rhs);
    case SlNode_NumInt:
    case SlNode_BoolLit:
    case SlNode_NullLit:
        return true;
    case SlNode_Access:
        switch (refVar(p, node->as.access.name)) {
        case Ref_failed:
            setErrorWLine(
                p,
                node->line,
                "unknown variable '"S_Fmt"'",
                S_Arg(node->as.access.name, p->tokens.strs)
            );
            return false;
        case Ref_nonlocal:
            node->as.access.local = false;
            // fallthrough
        case Ref_local:
            return true;
        }
    case SlNode_Assign:
        if (!resolveVars(p, node->as.assign.value)) return false;
        switch (refVar(p, node->as.assign.name)) {
        case Ref_failed:
            setErrorWLine(
                p,
                node->line,
                "unknown variable '"S_Fmt"'",
                S_Arg(node->as.access.name, p->tokens.strs)
            );
        case Ref_nonlocal:
            node->as.assign.local = false;
            // fallthrough
        case Ref_local:
            return true;
        }
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
    SlStrMap *vars = node->as.block.vars;
    if (vars == NULL) {
        vars = memAllocZeroed(1, sizeof(*vars));
        if (vars == NULL) {
            slSetOutOfMemoryError(p->vm);
            return false;
        }
        vars->userData = p->tokens.strs;
        node->as.block.vars = vars;
    }
    VarTable vt = {
        .parent = p->vt,
        .funcLevel = p->funcLevel,
        .vars = vars,
        .sharedCount = 0
    };

    // ReSharper disable once CppDFALocalValueEscapesFunction
    p->vt = &vt; // never used outside nested calls of this function
    p->vars = vars;

    node->as.block.funcCount = vars->len;

    for (uint32_t i = 0; i < node->as.block.nodeCount; i++) {
        if (!resolveVars(p, node->as.block.nodes[i])) return false;
    }
    node->as.block.sharedCount = p->vt->sharedCount;

    p->vt = vt.parent;
    p->vars = vt.parent ? vt.parent->vars : NULL;

    return true;
}
