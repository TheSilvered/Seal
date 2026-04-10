#include <stdarg.h>
#include <string.h>

#include "sl_array.h"
#include "sl_codegen.h"
#include "sl_parser.h"

// There are 0x7fff + 0x80 available registers but the top 128 are reserved
#define _maxReg 0x7fff
#define _maxConst 0xffffff
#define S_Fmt "%.*s"
#define S_Arg(str) (int)(str).len, (char *)(g->ast.strs + (str).idx)

slArrayType(SlObj, Constants, consts)
slArrayImpl(SlObj, Constants, consts)

// The program tracked as a stack of functions, stored in FuncState
// Each function has inside its own vars table that is a stack of blocks

typedef struct BlockState {
    struct BlockState *parent;
    SlStrMap *vars;
    uint16_t baseReg, baseShr;
} BlockState;

typedef struct FuncState {
    struct FuncState *parent;
    SlU8Arr bytecode;
    Constants consts;
    SlStrMap externalVars; // value: [fromShared?:1|src:15|0|dst:15]
    uint16_t usedStack;
    uint16_t maxStackSize;
    BlockState *block;
} FuncState;

typedef struct GenState {
    SlVM *vm;
    const char *path;
    SlAst ast;
    FuncState *func;
    int16_t outReg; // always absolute
} GenState;

static void emitU8(const GenState *g, uint8_t n);
static void emitI8(const GenState *g, int8_t n);
static void emitU16(const GenState *g, uint16_t n);
static void emitI24(const GenState *g, int32_t n);
static void emitU24(const GenState *g, int32_t n);
static void emitOp(const GenState *g, SlOpCode opCode);
// Use absolute register
static void emitRegAbs(const GenState *g, int16_t reg);
// Use reg + g->func->block->baseReg
static void emitRegRel(const GenState *g, uint16_t reg);
// Emit appropriate SlOp_lk*, use g->outReg for dst
static void emitLk(const GenState *g, int32_t src);

static void setError(const GenState *g, SlNodeIdx node, const char *fmt, ...);

static uint16_t getSlot(const GenState *g);
static bool useSlots(const GenState *g, SlNodeIdx node, size_t count);
static void releaseSlots(const GenState *g, uint16_t first);

static int16_t setOutRegRel(GenState *g, int16_t reg);
static int16_t setOutRegAbs(GenState *g, int16_t reg);
// If g->outReg is not set, use a new slot and set outReg to point to it
static bool useOutRegNew(GenState *g, SlNodeIdx idx);

static int32_t addConst(const GenState *g, SlNodeIdx node, SlObj obj);

static SlNode *getNode(const GenState *g, SlNodeIdx idx);

static SlObj genNamedLambda(GenState *g, SlNodeIdx idx, SlStrIdx name);
static bool genStmnt(GenState *g, SlNodeIdx idx);
static void genBlock(GenState *g, SlNodeIdx idx);
static void genVarDeclr(GenState *g, SlNodeIdx idx);
static void genPrint(GenState *g, SlNodeIdx idx);
static void genRetStmnt(GenState *g, SlNodeIdx idx);

// g->outReg contains the register where the value of the expression is stored

static bool genExpr(GenState *g, SlNodeIdx idx);
static void genLambda(GenState *g, SlNodeIdx idx);
static void genBinOp(GenState *g, SlNodeIdx idx);
static void genNumInt(GenState *g, SlNodeIdx idx);
static void genAccess(GenState *g, SlNodeIdx idx);

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

    SlObj main = genNamedLambda(&g, ast.root, (SlStrIdx){ .idx = 0, .len = 0 });
    slDestroyAst(&ast);
    if (main.type == SlObj_Prototype && main.as.proto->debugInfo != NULL) {
        main.as.proto->debugInfo->name = (uint8_t *)".main";
    }

    return main;
}

static void emitU8(const GenState *g, uint8_t n) {
    assert(g->func != NULL);
    slU8Push(g->vm, &g->func->bytecode, n);
}

static void emitI8(const GenState *g, int8_t n) {
    emitU8(g, (uint8_t)n);
}

static void emitU16(const GenState *g, uint16_t n) {
    emitU8(g, n >> 8);
    emitU8(g, n & 0xff);
}

static void emitU24(const GenState *g, int32_t n) {
    assert(n < 0x1000000);
    emitU8(g, (n >> 16) & 0xff);
    emitU8(g, (n >>  8) & 0xff);
    emitU8(g, (n >>  0) & 0xff);
}

static void emitI24(const GenState *g, int32_t n) {
    assert(n < 0x800000 && n >= -0x800000);
    emitU24(g, n);
}

static void emitOp(const GenState *g, SlOpCode opCode) {
    emitU8(g, (uint8_t)opCode);
}

static void emitRegRel(const GenState *g, uint16_t reg) {
    assert(g->func != NULL);
    assert(g->func->block != NULL);
    uint16_t baseReg = g->func->block->baseReg;

    assert((uint32_t)reg + (uint32_t)baseReg < 0xffff);
    emitRegAbs(g, (int16_t)(reg) + baseReg);
}

static void emitRegAbs(const GenState *g, int16_t reg) {
    assert(reg > 0 && reg <= _maxReg);
    if (reg < 0x80) {
        emitU8(g, (uint8_t)reg);
    } else {
        emitU16(g, (reg - 0x80) | 0x8000);
    }
}

static void emitLk(const GenState *g, int32_t src) {
    assert(src <= _maxConst);
    assert(g->outReg >= 0);
    if (src <= 0xff) {
        emitOp(g, SlOp_lkb);
        emitRegAbs(g, g->outReg);
        emitU8(g, (uint8_t)src);
    } else if (src <= 0xffff) {
        emitOp(g, SlOp_lks);
        emitRegAbs(g, g->outReg);
        emitU16(g, (uint16_t)src);
    } else {
        emitOp(g, SlOp_lki);
        emitRegAbs(g, g->outReg);
        emitU24(g, src);
    }
}

static void setError(const GenState *g, SlNodeIdx node, const char *fmt, ...) {
    if (g->vm->error.occurred) return;
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

static bool useSlots(const GenState *g, SlNodeIdx node, size_t count) {
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
    assert(g->func != NULL);
    assert(first < g->func->usedStack);
    g->func->usedStack = first;
}

static int16_t setOutRegRel(GenState *g, int16_t reg) {
    assert(g->func != NULL);
    assert(g->func->block != NULL);
    uint16_t baseReg = g->func->block->baseReg;

    if (reg < 0) return setOutRegAbs(g, -1);
    assert((uint32_t)reg + (uint32_t)baseReg < 0xffff);
    return setOutRegAbs(g, reg + baseReg);
}

static int16_t setOutRegAbs(GenState *g, int16_t reg) {
    if (reg < -1) reg = -1;
    assert(reg < _maxReg);
    int16_t old = g->outReg;
    g->outReg = reg;
    return old;
}

static bool useOutRegNew(GenState *g, SlNodeIdx idx) {
    if (g->outReg >= 0) return true;
    g->outReg = (int16_t)getSlot(g);
    return useSlots(g, idx, 1);
}

static int32_t addConst(const GenState *g, SlNodeIdx node, SlObj obj) {
    assert(g->func != NULL);
    if (g->func->consts.len >= _maxConst) {
        setError(
            g,
            node,
            "maximum number of constants for function exceeded (max is %d)",
            _maxConst
        );
        return -1;
    }
    if (!constsPush(g->vm, &g->func->consts, obj)) return -1;
    return (int32_t)(g->func->consts.len - 1);
}

static SlNode *getNode(const GenState *g, SlNodeIdx idx) {
    assert(idx >= 0 && (uint32_t)idx < g->ast.nodeCount);
    return &g->ast.nodes[idx];
}

static bool genStmnt(GenState *g, SlNodeIdx idx) {
    if (g->vm->error.occurred) return false;

    SlNode *node = getNode(g, idx);
    uint16_t firstSlot = getSlot(g);
    switch (node->kind) {
    case SlNode_INVALID:
    case SlNode_BinOp:
    case SlNode_NumInt:
    case SlNode_Access:
    case SlNode_Lambda:
        assert(false && "unreachable");
        return false;
    case SlNode_Block:
        genBlock(g, idx);
        break;
    case SlNode_VarDeclr:
        genVarDeclr(g, idx);
        break;
    case SlNode_Print:
        genPrint(g, idx);
        break;
    case SlNode_RetStmnt:
        genRetStmnt(g, idx);
        break;
    }
    // When a statement ends, the number of slots used after is the same as the
    // number of slots used before (since variables are pre-allocated)
    releaseSlots(g, firstSlot);
    g->outReg = -1;
    return g->vm->error.occurred;
}

static void genBlock(GenState *g, SlNodeIdx idx) {
    assert(g->func != NULL);
    assert(g->ast.nodes[idx].kind == SlNode_Block);

    SlNode *node = getNode(g, idx);
    uint16_t varCount = node->as.block.vars->len;
    uint16_t funcCount = node->as.block.funcCount;
    uint16_t sharedCount = node->as.block.sharedCount;
    uint16_t baseReg = getSlot(g);

    BlockState newBlockState = {
        .parent = g->func->block,
        .vars = getNode(g, idx)->as.block.vars,
        .baseReg = baseReg,
        .baseShr = baseReg + varCount
    };
    if (!useSlots(g, idx, varCount + sharedCount)) return;

    // ReSharper disable once CppDFALocalValueEscapesFunction
    g->func->block = &newBlockState;

    if (funcCount != 0) {
        emitOp(g, SlOp_ln);
        emitRegRel(g, 0);
        emitRegRel(g, funcCount - 1);
    }
    slMapForeach(node->as.block.vars, SlStrMapBucket, var, i) {
        if (i >= funcCount) goto break_foreach;
        int32_t shrIdx = ((int32_t)var->value >> 16) - 1;
        if (shrIdx == -1) continue;
        emitOp(g, SlOp_mks);
        emitRegRel(g, (uint16_t)shrIdx + varCount);
        emitRegRel(g, (uint16_t)i);
    }
break_foreach:

    for (uint32_t i = 0; i < node->as.block.nodeCount; i++) {
        if (!genStmnt(g, node->as.block.nodes[i])) return;
    }

    if (sharedCount != 0) {
        emitOp(g, SlOp_dts);
        emitRegRel(g, varCount);
        emitRegRel(g, varCount + sharedCount - 1);
    }
    releaseSlots(g, g->outReg);
    g->func->block = newBlockState.parent;
}

static void genVarDeclr(GenState *g, SlNodeIdx idx) {
    SlNode *node = getNode(g, idx);
    uint32_t *varInfo = slStrMapGet(
        g->func->block->vars,
        node->as.varDeclr.name
    );
    assert(varInfo != NULL);

    uint16_t slotIdx = *varInfo & 0xff;
    int16_t shrIdx = (int16_t)(*varInfo >> 16) - 1;

    int16_t oldOutReg = setOutRegRel(g, slotIdx);

    SlNode *value = getNode(g, node->as.varDeclr.value);
    if (value->kind == SlNode_Lambda) {
        SlObj lambda = genNamedLambda(
            g,
            node->as.varDeclr.value,
            node->as.varDeclr.name
        );
        if (lambda.type == SlObj_Null) return;
        int32_t constIdx = addConst(g, node->as.varDeclr.value, lambda);
        if (constIdx == -1) {
            slDelRef(lambda);
            return;
        }
        emitLk(g, constIdx);
    } else {
        if (!genExpr(g, node->as.varDeclr.value)) return;
    }

    g->outReg = oldOutReg;
    if (shrIdx >= 0) {
        emitOp(g, SlOp_mks);
        emitRegRel(g, g->func->block->baseShr + shrIdx);
        emitRegRel(g, slotIdx);
    }
}

static void genPrint(GenState *g, SlNodeIdx idx) {
    emitOp(g, SlOp_print);
    if (!genExpr(g, getNode(g, idx)->as.print)) return;
    emitRegAbs(g, g->outReg);
}

static void genRetStmnt(GenState *g, SlNodeIdx idx) {
    emitOp(g, SlOp_ret);
    if (!genExpr(g, getNode(g, idx)->as.print)) return;
    emitRegAbs(g, g->outReg);
}

static SlObj genNamedLambda(GenState *g, SlNodeIdx idx, SlStrIdx name) {
    (void)name; // TODO: generate line info

    FuncState newTop = {
        .parent = g->func,
        .externalVars = { .userData = g->ast.strs },
    };

    // ReSharper disable once CppDFALocalValueEscapesFunction
    g->func = &newTop;

    SlNodeIdx body = g->ast.nodes[idx].as.lambda.body;
    assert(g->ast.nodes[body].kind == SlNode_Block);

    if (!genStmnt(g, body)) return slNull;

    SlSharedInfo *sharedInfo = memAllocZeroed(
        newTop.externalVars.len,
        sizeof(*sharedInfo)
    );
    if (sharedInfo == NULL) {
        slSetOutOfMemoryError(g->vm);
        return slNull;
    }

    slMapForeach(&newTop.externalVars, SlStrMapBucket, var, mapIdx) {
        uint16_t i = var->value & 0x7fff;
        sharedInfo[i] = (SlSharedInfo){
            .fromShared = var->value >> 31,
            .idx = (var->value >> 16) & 0x7fff
        };
    }

    g->func = newTop.parent;
    return slPrototypeNew(
        g->vm,
        newTop.bytecode.data,
        newTop.bytecode.len,
        newTop.consts.data,
        newTop.consts.len,
        sharedInfo,
        newTop.externalVars.len,
        newTop.maxStackSize,
        NULL
    );
}

static bool genExpr(GenState *g, SlNodeIdx idx) {
    if (g->vm->error.occurred) return false;
    SlNode *node = getNode(g, idx);
    switch (node->kind) {
    case SlNode_Access:
        genAccess(g, idx);
        break;
    case SlNode_BinOp:
        genBinOp(g, idx);
        break;
    case SlNode_NumInt:
        genNumInt(g, idx);
        break;
    case SlNode_Lambda:
        genLambda(g, idx);
        break;
    case SlNode_INVALID:
    case SlNode_Block:
    case SlNode_VarDeclr:
    case SlNode_Print:
    case SlNode_RetStmnt:
        assert(false && "unreachable");
        return false;
    }
    return g->vm->error.occurred;
}

static void genLambda(GenState *g, SlNodeIdx idx) {
    if (!useOutRegNew(g, idx)) return;
    SlObj lambda = genNamedLambda(g, idx, (SlStrIdx){ .idx = 0, .len = 0 });
    if (lambda.type == SlObj_Null) return;
    int32_t constIdx = addConst(g, idx, lambda);
    if (constIdx < 0) {
        slDelRef(lambda);
        return;
    }
    emitLk(g, constIdx);
}

static void genBinOp(GenState *g, SlNodeIdx idx) {
    if (!useOutRegNew(g, idx)) return;
    uint16_t dst = setOutRegAbs(g, -1);
    SlNode *node = getNode(g, idx);
    if (!genExpr(g, node->as.binOp.lhs)) return;
    uint16_t lhs = setOutRegAbs(g, -1);
    if (!genExpr(g, node->as.binOp.rhs)) return;
    uint16_t rhs = setOutRegAbs(g, dst);

    switch (node->as.binOp.op) {
    case SlBinOp_Add:
        emitOp(g, SlOp_add);
        break;
    case SlBinOp_Sub:
        emitOp(g, SlOp_sub);
        break;
    case SlBinOp_Mul:
        emitOp(g, SlOp_mul);
        break;
    case SlBinOp_Div:
        emitOp(g, SlOp_div);
        break;
    case SlBinOp_Mod:
        emitOp(g, SlOp_mod);
        break;
    case SlBinOp_Pow:
        emitOp(g, SlOp_pow);
        break;
    }

    emitRegAbs(g, dst);
    emitRegAbs(g, lhs);
    emitRegAbs(g, rhs);
}

static void genNumInt(GenState *g, SlNodeIdx idx) {
    int64_t num = getNode(g, idx)->as.numInt;
    if (!useOutRegNew(g, idx)) return;
    if (num > -128 && num < 127) {
        emitOp(g, SlOp_li8);
        emitRegAbs(g, g->outReg);
        emitI8(g, (int8_t)num);
    } else {
        int32_t constIdx = addConst(g, idx, slObjInt(num));
        if (constIdx < 0) return;
        emitLk(g, constIdx);
    }
}

static void genAccess(GenState *g, SlNodeIdx idx) {

}
