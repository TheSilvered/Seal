#include <stdarg.h>
#include <string.h>

#include "sl_array.h"
#include "sl_codegen.h"
#include "sl_parser.h"

#define _maxReg (0x7fff + 0x80)
#define S_Fmt "%.*s"
#define S_Arg(str) (int)(str).len, (char *)(g->ast.strs + (str).idx)

slArrayType(SlObj, Constants, consts)
slArrayImpl(SlObj, Constants, consts)

// The program tracked as a stack of functions, stored in FuncState
// Each function has inside its own vars table that is a stack of blocks

typedef struct Vars {
    struct Vars *parent;
    SlNodeIdx blockIdx;
} Vars;

typedef struct FuncState {
    struct FuncState *parent;
    SlU8Arr bytecode;
    Constants consts;
    SlStrMap externalVars;
    uint16_t usedStack;
    uint16_t maxStackSize;
    Vars *vars;
} FuncState;

typedef struct GenState {
    SlVM *vm;
    const char *path;
    SlAst ast;
    FuncState *func;
} GenState;

static bool emitU8(const GenState *g, uint8_t n);
static bool emitI8(const GenState *g, int8_t n);
static bool emitU16(const GenState *g, uint16_t n);
static bool emitI24(const GenState *g, int32_t n);
static bool emitOp(const GenState *g, SlOpCode opCode);
static bool emitReg(const GenState *g, uint16_t reg);

static void setError(const GenState *g, SlNodeIdx node, const char *fmt, ...);

static uint16_t getSlot(const GenState *g);
static bool useSlots(const GenState *g, SlNodeIdx node, uint16_t count);
static void releaseSlots(const GenState *g, uint16_t first);

static SlNode *getNode(const GenState *g, SlNodeIdx idx);

static bool genStmnt(GenState *g, SlNodeIdx idx);
static bool genBlock(GenState *g, SlNodeIdx idx);
static SlObj genLambda(GenState *g, SlNodeIdx idx, const char *name);

SlObj slGenCode(SlVM *vm, const SlSource *source) {
    SlAst ast = slParse(vm, source);
    if (vm->error.occurred) {
        return slNull;
    }
    assert(ast.nodes[ast.root].kind == SlNode_Lambda);

    GenState g = {
        .vm = vm,
        .ast = ast,
        .path = source->path,
        .func = NULL
    };
}

static bool emitU8(const GenState *g, uint8_t n) {
    assert(g->func != NULL);
    return slU8Push(g->vm, &g->func->bytecode, n);
}

static bool emitI8(const GenState *g, int8_t n) {
    return emitU8(g, (uint8_t)n);
}

static bool emitU16(const GenState *g, uint16_t n) {
    return emitU8(g, n >> 8) && emitU8(g, n & 0xff);
}

static bool emitU24(const GenState *g, uint32_t n) {
    assert(n < 0x800000 && n >= -0x800000);
    return emitU8(g, (n >> 16) & 0xff)
        && emitU8(g, (n >>  8) & 0xff)
        && emitU8(g, (n >>  0) & 0xff);
}

static bool emitOp(const GenState *g, SlOpCode opCode) {
    return emitU8(g, (uint8_t)opCode);
}

static bool emitReg(const GenState *g, uint16_t reg) {
    assert(reg <= _maxReg);
    if (reg < 0x80) {
        return emitU8(g, (uint8_t)reg);
    } else {
        return emitU16(g, (reg - 0x80) | 0x8000);
    }
}

static void setError(const GenState *g, SlNodeIdx node, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[64];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    uint32_t line = g->ast.nodes[node].line;
    slSetError(g->vm, "%s:%"PRIu32": %s", g->path, line, buf);
}

static uint16_t getSlot(const GenState *g) {
    assert(g->func != NULL);
    return g->func->usedStack;
}

static bool useSlots(const GenState *g, SlNodeIdx node, uint16_t count) {
    assert(g->func != NULL);
    if (g->func->usedStack > _maxReg - count) {
        setError(
            g,
            node,
            "maximum function frame size exceeded (max is %d)",
            _maxReg
        );
        return false;
    }

    g->func->usedStack += count;
    if (g->func->usedStack > g->func->maxStackSize) {
        g->func->maxStackSize = g->func->usedStack;
    }
    return true;
}

static void releaseSlots(const GenState *g, uint16_t first) {
    assert(g->func != NULL && first < g->func->usedStack);
    g->func->usedStack = first;
}

static SlNode *getNode(const GenState *g, SlNodeIdx idx) {
    assert(idx >= 0 && idx < g->ast.nodeCount);
    return &g->ast.nodes[idx];
}

static bool genStmnt(GenState *g, SlNodeIdx idx) {
    SlNode *node = getNode(g, idx);
    switch (node->kind) {
    case SlNode_INVALID:
        assert(false && "unreachable");
        return false;
    case SlNode_Block:
        return genBlock(g, idx);
    case SlNode_VarDeclr:
        return genVarDeclr(g, idx);
    case SlNode_BinOp:
        return genBinOp(g, idx);
    case SlNode_NumInt:
    case SlNode_Access:
    case SlNode_Lambda:
        return true;
    case SlNode_Print:
        return genPrint(g, idx);
    case SlNode_RetStmnt:
        return genRetStmnt(g, idx);
    }
}

static bool genBlock(GenState *g, SlNodeIdx idx) {
    assert(g->func != NULL);
    assert(g->ast.nodes[idx].kind == SlNode_Block);

    Vars newVars = {
        .parent = g->func->vars,
        .blockIdx = idx
    };
    g->func->vars = &newVars;

    SlNode *block = getNode(g, idx);

    for (uint32_t i = 0; i < block->as.block.nodeCount; i++) {
        if (!genStmnt(g, block->as.block.nodes[i])) {
            return false;
        }
    }

    g->func->vars = newVars.parent;
    return true;
}

static bool genLambda(GenState *g, SlNodeIdx idx, const char *name) {
    FuncState newTop = {
        .parent = g->func,
        .bytecode = { 0 },
        .consts = { 0 },
        .externalVars = { .userData = g->ast.strs },
        .usedStack = 0,
        .maxStackSize = 0,
        .vars = NULL
    };

    g->func = &newTop;

    SlNodeIdx body = g->ast.nodes[idx].as.lambda.body;
    assert(g->ast.nodes[body].kind == SlNode_Block);

    if (!genBlock(g, body)) {
        return false;
    }

    SlSharedSlot *slots = memAllocZeroed(
        newTop.externalVars.len,
        sizeof(*slots)
    );
    if (slots == NULL) {
        slSetOutOfMemoryError(g->vm);
        return false;
    }

    slMapForeach(newTop.externalVars, SlStrMapBucket, var) {
        uint16_t idx = 
    }

    g->func = newTop.parent;
    slPrototypeNew(g->vm, newTop.bytecode.data, newTop.bytecode.len, newTop.consts.data, newTop.consts.len, newTop.)
}
