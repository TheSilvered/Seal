#include <stdint.h>
#include <string.h>

#include "sl_array.h"
#include "sl_codegen.h"
#include "sl_parser.h"

// There are 0x7fff + 0x80 available registers but the top 128 are reserved
#define _maxReg 0x7fff
#define _maxConst 0xffffff
#define _maxJump 0x7fffff
#define S_Fmt "%.*s"
#define S_Arg(str) (int)(str).len, (char *)(g->ast.strs + (str).idx)

slArrayType(SlObj, Constants, consts)
slArrayImpl(SlObj, Constants, consts)

enum ValsArr {
    Vals_false,
    Vals_true,
    Vals_null
};

SlOpCode inverseTest[] = {
    [SlOp_teq  - SlOp_teq] = SlOp_tne,
    [SlOp_teqi - SlOp_teq] = SlOp_tnei,
    [SlOp_tne  - SlOp_teq] = SlOp_teq,
    [SlOp_tnei - SlOp_teq] = SlOp_teqi,
    [SlOp_tlt  - SlOp_teq] = SlOp_tge,
    [SlOp_tlti - SlOp_teq] = SlOp_tgei,
    [SlOp_tle  - SlOp_teq] = SlOp_tgt,
    [SlOp_tlei - SlOp_teq] = SlOp_tgti,
    [SlOp_tgt  - SlOp_teq] = SlOp_tle,
    [SlOp_tgti - SlOp_teq] = SlOp_tlei,
    [SlOp_tge  - SlOp_teq] = SlOp_tlt,
    [SlOp_tgei - SlOp_teq] = SlOp_tlti,
    [SlOp_ttr  - SlOp_teq] = SlOp_tfl,
    [SlOp_tfl  - SlOp_teq] = SlOp_ttr,
    [SlOp_tnl  - SlOp_teq] = SlOp_tnnl,
    [SlOp_tnnl - SlOp_teq] = SlOp_tnl
};

// The program tracked as a stack of functions, stored in FuncState
// Each function has inside its own vars table that is a stack of blocks

typedef struct BlockState {
    struct BlockState *parent;
    SlStrMap *vars;
    uint16_t baseReg, baseShr;
} BlockState;

typedef struct FuncState {
    struct FuncState *parent;
    SlU32Arr bytecode;
    Constants consts;
    SlStrMap externalVars; // value: [fromShared?:1|src:15|0|dst:15]
    uint16_t usedStack;
    uint16_t maxStackSize;
    BlockState *block;
    #define isShrVarFromShared(info) ((bool)(info >> 31))
    #define shrVarSrc(info) ((uint16_t)(info >> 16) & 0x7f)
    #define shrVarDst(info) ((uint16_t)(info & 0x7f))
} FuncState;

typedef struct GenState {
    SlVM *vm;
    const char *path;
    SlAst ast;
    FuncState *func;
    int16_t outReg; // always absolute
} GenState;

static bool emitA(GenState *g, SlOpCode op, uint16_t rd, uint16_t r1, uint16_t r2);
// static bool emitKu(GenState *g, SlOpCode op, uint16_t rd, uint16_t r1, uint16_t imm);
static bool emitKs(GenState *g, SlOpCode op, uint16_t rd, uint16_t r1, int16_t imm);
static bool emitIu(GenState *g, SlOpCode op, uint16_t rd, uint32_t imm);
static bool emitIs(GenState *g, SlOpCode op, uint16_t rd, int32_t imm);
static bool emitO(GenState *g, SlOpCode op, uint16_t rd);
static bool emitT(GenState *g, SlOpCode op, uint16_t rd, uint16_t r1);
static bool emitJ(GenState *g, int32_t offset);
static uint32_t jumpPlaceholder(GenState *g);
static bool jumpTo(GenState *g, uint32_t placeholder, uint32_t goal);

static uint32_t getPos(const GenState *g);
// Transform a register relative to the block into an absolute register
// (relative to the function)
static uint16_t absReg(const GenState *g, uint16_t relReg);

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

static SlObj genProtoObj(GenState *g, SlNodeIdx idx, SlStrIdx name);

static bool genStmnt(GenState *g, SlNodeIdx idx);
static void genBlock(GenState *g, SlNodeIdx idx);
static void genVarDeclr(GenState *g, SlNodeIdx idx);
static void genIfStmnt(GenState *g, SlNodeIdx idx);
static void genWhileLoop(GenState *g, SlNodeIdx idx);
static void genPrint(GenState *g, SlNodeIdx idx);
static void genRetStmnt(GenState *g, SlNodeIdx idx);

// g->outReg contains the register where the value of the expression is stored

// If idx is true the next instruction is taken
// static bool testTrue(GenState *g, SlNodeIdx idx);
// If idx is false the next instruction is taken
static bool testFalse(GenState *g, SlNodeIdx idx);

static bool genExpr(GenState *g, SlNodeIdx idx);
static void genLambda(GenState *g, SlNodeIdx idx, SlStrIdx name);
static void genBinOp(GenState *g, SlNodeIdx idx);
static void genBinOpEx(GenState *g, SlNodeIdx idx, SlOpCode op);
static void genNumInt(GenState *g, SlNodeIdx idx);
static void genBoolLit(GenState *g, SlNodeIdx idx);
static void genNullLit(GenState *g, SlNodeIdx idx);
static void genAccess(GenState *g, SlNodeIdx idx);
static void genAssign(GenState *g, SlNodeIdx idx);
static void genFuncCall(GenState *g, SlNodeIdx idx);

void printPrototype(SlObj main);

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

    SlObj mainProto = genProtoObj(&g, ast.root, (SlStrIdx){ .idx = 0, .len = 0 });
    slDestroyAst(&ast);
    if (
        mainProto.type == SlObj_Prototype
        && mainProto.as.proto->debugInfo != NULL
    ) {
        mainProto.as.proto->debugInfo->name = (uint8_t *)".main";
    } else if (mainProto.type == SlObj_Null) {
        return slNull;
    }

    char *printBytecode = getenv("SL_PRINT_BC");
    if (printBytecode && strcmp(printBytecode, "true") == 0) {
        printPrototype(mainProto);
    }

    return slSimpleFuncNew(vm, mainProto);
}

static bool emitA(
    GenState *g,
    SlOpCode op,
    uint16_t rd, uint16_t r1, uint16_t r2
) {
    bool extended = (rd > 0xff) || (r1 > 0xff) || (r2 > 0xff);
    uint32_t inst1 =
          ((r2 & 0xff) << 24)
        | ((r1 & 0xff) << 16)
        | ((rd & 0xff) << 8)
        | (op << 1)
        | extended;

    if (!slU32Push(g->vm, &g->func->bytecode, inst1)) return false;
    if (!extended) return true;

    uint32_t inst2 =
          ((r2 & 0xff00) << 16)
        | ((r1 & 0xff00) << 8)
        | ((rd & 0xff00))
        | 0xfe;

    return slU32Push(g->vm, &g->func->bytecode, inst2);
}

#if 0
static bool emitKu(
    GenState *g,
    SlOpCode op,
    uint16_t rd, uint16_t r1, uint16_t imm
) {
    return emitA(g, op, rd, r1, imm);
}
#endif

static bool emitKs(
    GenState *g,
    SlOpCode op,
    uint16_t rd, uint16_t r1, int16_t imm
) {
    if (imm >= -128 && imm <= 127) {
        return emitA(g, op, rd, r1, (uint16_t)imm & 0xff);
    } else {
        return emitA(g, op, rd, r1, (uint16_t)imm);
    }
}

static bool emitIu(GenState *g, SlOpCode op, uint16_t rd, uint32_t imm) {
    bool extended = imm > 0xff;
    uint32_t inst1 = ((imm & 0xff) << 24) | (rd << 8) | (op << 1) | extended;

    if (!slU32Push(g->vm, &g->func->bytecode, inst1)) return false;
    if (!extended) return true;

    uint32_t inst2 = (imm << 8) | 0xfe;
    return slU32Push(g->vm, &g->func->bytecode, inst2);
}

static bool emitIs(GenState *g, SlOpCode op, uint16_t rd, int32_t imm) {
    if (imm >= -128 && imm <= 127) {
        return emitIu(g, op, rd, (uint32_t)imm & 0xff);
    } else {
        return emitIu(g, op, rd, (uint32_t)imm);
    }
}

static bool emitT(GenState *g, SlOpCode op, uint16_t rd, uint16_t r1) {
    bool extended = rd > 0xff;
    uint32_t inst1 = (r1 << 16) | ((rd & 0xff) << 8) | (op << 1) | extended;

    if (!slU32Push(g->vm, &g->func->bytecode, inst1)) return false;
    if (!extended) return true;

    uint32_t inst2 = (rd & 0xff00) | 0xfe;
    return slU32Push(g->vm, &g->func->bytecode, inst2);
}

static bool emitO(GenState *g, SlOpCode op, uint16_t rd) {
    return emitIu(g, op, rd, 0);
}

static bool emitJ(GenState *g, int32_t offset) {
    return slU32Push(
        g->vm,
        &g->func->bytecode,
        (offset << 8) | (SlOp_jmp << 1)
    );
}

static uint32_t jumpPlaceholder(GenState *g) {
    slU32Push(g->vm, &g->func->bytecode, SlOp_jmp << 1);
    return g->func->bytecode.len - 1;
}

static bool jumpTo(GenState *g, uint32_t placeholder, uint32_t goal) {
    uint32_t start = placeholder + 1;
    uint32_t dist = goal > start ? goal - start : start - goal;

    if (dist > _maxJump) {
        return false;
    }

    int32_t offset;
    if (goal > placeholder) {
        offset = (int32_t)dist;
    } else {
        offset = -(int32_t)dist;
    }
    g->func->bytecode.data[placeholder] |= (offset << 8);
    return true;
}

static uint32_t getPos(const GenState *g) {
    assert(g->func != NULL);
    return g->func->bytecode.len;
}

static uint16_t absReg(const GenState *g, uint16_t relReg) {
    return relReg + g->func->block->baseReg;
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
    assert(first <= g->func->usedStack);
    g->func->usedStack = first;
}

static int16_t setOutRegRel(GenState *g, int16_t reg) {
    assert(g->func != NULL);
    assert(g->func->block != NULL);
    uint16_t baseReg = g->func->block->baseReg;

    if (reg < 0) return setOutRegAbs(g, -1);
    assert((uint32_t)reg + (uint32_t)baseReg <= _maxReg);
    return setOutRegAbs(g, (int16_t)(reg + baseReg));
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
    g->outReg = -1;

    SlNode *node = getNode(g, idx);
    uint16_t firstSlot = getSlot(g);
    switch (node->kind) {
    case SlNode_INVALID:
    case SlNode_BinOp:
    case SlNode_NumInt:
    case SlNode_BoolLit:
    case SlNode_NullLit:
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
    case SlNode_IfStmnt:
        genIfStmnt(g, idx);
        break;
    case SlNode_WhileLoop:
        genWhileLoop(g, idx);
        break;
    case SlNode_Print:
        genPrint(g, idx);
        break;
    case SlNode_RetStmnt:
        genRetStmnt(g, idx);
        break;
    case SlNode_Assign:
        genAssign(g, idx);
        break;
    case SlNode_FuncCall:
        genFuncCall(g, idx);
        break;
    }
    // When a statement ends, the number of slots used after is the same as the
    // number of slots used before (since variables are pre-allocated)
    releaseSlots(g, firstSlot);
    return !g->vm->error.occurred;
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
        emitIu(g, SlOp_ldn, absReg(g, 0), funcCount);
    }

    // Create the shared slots for shared functions
    slMapForeach(node->as.block.vars, SlStrMapBucket, var, i) {
        if (i >= funcCount) goto break_foreach;
        int32_t shrIdx = ((int32_t)var->value >> 16) - 1;
        if (shrIdx == -1) continue;
        emitT(
            g,
            SlOp_mksh,
            absReg(g, (uint16_t)shrIdx + varCount),
            absReg(g, (uint16_t)i)
        );
    }
break_foreach:

    for (uint32_t i = 0; i < node->as.block.nodeCount; i++) {
        if (!genStmnt(g, node->as.block.nodes[i])) return;
    }

    if (sharedCount != 0) {
        emitIu(g, SlOp_dtsh, absReg(g, varCount), sharedCount);
    }
    releaseSlots(g, baseReg);
    g->func->block = newBlockState.parent;
}

static void genVarDeclr(GenState *g, SlNodeIdx idx) {
    SlNode *node = getNode(g, idx);
    uint32_t *varInfo = slStrMapGet(
        g->func->block->vars,
        node->as.varDeclr.name
    );
    assert(varInfo != NULL);

    uint16_t slotIdx = slVarIdx(*varInfo);
    int16_t shrIdx = slVarShr(*varInfo);

    int16_t oldOutReg = setOutRegRel(g, (int16_t)slotIdx);

    SlNode *value = getNode(g, node->as.varDeclr.value);
    if (value->kind == SlNode_Lambda) {
        genLambda(g, node->as.varDeclr.value, node->as.varDeclr.name);
        if (g->vm->error.occurred) return;
    } else {
        if (!genExpr(g, node->as.varDeclr.value)) return;
    }

    g->outReg = oldOutReg;
    if (shrIdx >= 0) {
        emitT(
            g,
            SlOp_mksh,
            absReg(g, g->func->block->baseShr + shrIdx),
            absReg(g, slotIdx)
        );
    }
}

static void genIfStmnt(GenState *g, SlNodeIdx idx) {
    SlNode *node = getNode(g, idx);
    if (!testFalse(g, node->as.ifStmnt.condition)) return;
    uint32_t toTrueEnd = jumpPlaceholder(g);
    if (!genStmnt(g, node->as.ifStmnt.ifTrue)) return;

    // If there is an else block
    if (node->as.ifStmnt.ifFalse >= 0) {
        uint32_t toFalseEnd = jumpPlaceholder(g);
        jumpTo(g, toTrueEnd, getPos(g));
        if (!genStmnt(g, node->as.ifStmnt.ifFalse)) return;
        jumpTo(g, toFalseEnd, getPos(g));
    } else {
        jumpTo(g, toTrueEnd, getPos(g));
    }
}

static void genWhileLoop(GenState *g, SlNodeIdx idx) {
    uint32_t condStart = getPos(g);
    if (!testFalse(g, getNode(g, idx)->as.whileLoop.condition)) return;
    uint32_t toBodyEnd = jumpPlaceholder(g);
    if (!genStmnt(g, getNode(g, idx)->as.whileLoop.body)) return;
    jumpTo(g, jumpPlaceholder(g), condStart);
    jumpTo(g, toBodyEnd, getPos(g));
}

static void genPrint(GenState *g, SlNodeIdx idx) {
    if (!genExpr(g, getNode(g, idx)->as.print)) return;
    emitO(g, SlOp_print, g->outReg);
}

static void genRetStmnt(GenState *g, SlNodeIdx idx) {
    SlNodeIdx expr = getNode(g, idx)->as.retStmnt;
    if (expr == -1) {
        emitIu(g, SlOp_retv, 0, Vals_null);
        return;
    }
    SlNode *exprNode = getNode(g, expr);
    if (exprNode->kind == SlNode_NullLit) {
        emitIu(g, SlOp_retv, 0, Vals_null);
    } else if (exprNode->kind == SlNode_BoolLit) {
        emitIu(g, SlOp_retv, 0, !!(exprNode->as.boolLit));
    } else {
        if (!genExpr(g, expr)) return;
        emitO(g, SlOp_ret, g->outReg);
    }
}

static SlObj genProtoObj(GenState *g, SlNodeIdx idx, SlStrIdx name) {
    (void)name; // TODO: generate line info

    FuncState newTop = {
        .parent = g->func,
        .externalVars = { .userData = g->ast.strs }
    };

    // ReSharper disable once CppDFALocalValueEscapesFunction
    g->func = &newTop;

    SlNodeIdx body = g->ast.nodes[idx].as.lambda.body;
    assert(g->ast.nodes[body].kind == SlNode_Block);

    if (!genStmnt(g, body)) return slNull;
    emitIu(g, SlOp_retv, 0, Vals_null);
    if (g->vm->error.occurred) return slNull;

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
        g->ast.nodes[idx].as.lambda.paramCount,
        NULL
    );
}

#define getT(test) (testFalse ? inverseTest[(test) - SlOp_teq] : (test))

static bool genTestBinOp(GenState *g, SlNodeIdx idx, bool testFalse) {
    switch (getNode(g, idx)->as.binOp.op) {
    case SlBinOp_Lt:
        genBinOpEx(g, idx, getT(SlOp_tlt));
        break;
    case SlBinOp_Le:
        genBinOpEx(g, idx, getT(SlOp_tle));
        break;
    case SlBinOp_Gt:
        genBinOpEx(g, idx, getT(SlOp_tgt));
        break;
    case SlBinOp_Ge:
        genBinOpEx(g, idx, getT(SlOp_tge));
        break;
    case SlBinOp_Eq:
        genBinOpEx(g, idx, getT(SlOp_teq));
        break;
    case SlBinOp_Ne:
        genBinOpEx(g, idx, getT(SlOp_tne));
        break;
    default:
        assert(false && "unreachable");
    }
    return !g->vm->error.occurred;
}

static bool genTest(GenState *g, SlNodeIdx idx, bool testFalse) {

    SlNode *node = getNode(g, idx);
    switch (node->kind) {
    case SlNode_NumInt:
        return testFalse ? emitJ(g, 1) : true;
    case SlNode_NullLit:
        return testFalse ? true : emitJ(g, 1);
    case SlNode_BoolLit:
        return node->as.boolLit ^ testFalse ? true : emitJ(g, 1);
    case SlNode_BinOp:
        if (slBinOpTestable(node->as.binOp.op)) {
            return genTestBinOp(g, idx, testFalse);
        }
        // fallthrough
    default: {
        uint16_t testSlots = getSlot(g);
        if (!genExpr(g, idx)) return false;
        if (!emitO(g, getT(SlOp_ttr), g->outReg)) return false;
        releaseSlots(g, testSlots);
        return true;
    }
    }

}

#undef getT

// static bool testTrue(GenState *g, SlNodeIdx idx) {
//     return genTest(g, idx, false);
// }

static bool testFalse(GenState *g, SlNodeIdx idx) {
    return genTest(g, idx, true);
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
    case SlNode_BoolLit:
        genBoolLit(g, idx);
        break;
    case SlNode_NullLit:
        genNullLit(g, idx);
        break;
    case SlNode_Lambda:
        genLambda(g, idx, (SlStrIdx){ 0 });
        break;
    case SlNode_Assign:
        genAssign(g, idx);
        break;
    case SlNode_FuncCall:
        genFuncCall(g, idx);
        break;
    case SlNode_INVALID:
    case SlNode_Block:
    case SlNode_VarDeclr:
    case SlNode_IfStmnt:
    case SlNode_WhileLoop:
    case SlNode_Print:
    case SlNode_RetStmnt:
        assert(false && "unreachable");
        return false;
    }
    return !g->vm->error.occurred;
}

static void genLambda(GenState *g, SlNodeIdx idx, SlStrIdx name) {
    uint16_t outReg = g->outReg;
    SlObj lambda = genProtoObj(g, idx, name);
    g->outReg = outReg;
    if (lambda.type == SlObj_Null) return;
    int32_t constIdx = addConst(g, idx, lambda);
    if (constIdx < 0) {
        slDelRef(lambda);
        return;
    }
    if (!useOutRegNew(g, idx)) return;
    emitIu(g, SlOp_mkf, g->outReg, constIdx);
}

static void genBinOp(GenState *g, SlNodeIdx idx) {
    switch (getNode(g, idx)->as.binOp.op) {
    case SlBinOp_Add:
        genBinOpEx(g, idx, SlOp_add);
        break;
    case SlBinOp_Sub:
        genBinOpEx(g, idx, SlOp_sub);
        break;
    case SlBinOp_Mul:
        genBinOpEx(g, idx, SlOp_mul);
        break;
    case SlBinOp_Div:
        genBinOpEx(g, idx, SlOp_div);
        break;
    case SlBinOp_Mod:
        genBinOpEx(g, idx, SlOp_mod);
        break;
    case SlBinOp_Pow:
        genBinOpEx(g, idx, SlOp_pow);
        break;
    case SlBinOp_Lt:
        genBinOpEx(g, idx, SlOp_lt);
        break;
    case SlBinOp_Le:
        genBinOpEx(g, idx, SlOp_le);
        break;
    case SlBinOp_Gt:
        genBinOpEx(g, idx, SlOp_gt);
        break;
    case SlBinOp_Ge:
        genBinOpEx(g, idx, SlOp_ge);
        break;
    case SlBinOp_Eq:
        genBinOpEx(g, idx, SlOp_eq);
        break;
    case SlBinOp_Ne:
        genBinOpEx(g, idx, SlOp_ne);
        break;
    }
}

static void genBinOpEx(GenState *g, SlNodeIdx idx, SlOpCode op) {
    SlNode *node = getNode(g, idx);
    SlNode *rhsNode = getNode(g, node->as.binOp.rhs);
    bool useImm = rhsNode->kind == SlNode_NumInt
        && rhsNode->as.numInt >= INT16_MIN
        && rhsNode->as.numInt <= INT16_MAX;

    uint16_t top = getSlot(g);
    int16_t dst = setOutRegAbs(g, -1);
    if (!genExpr(g, node->as.binOp.lhs)) return;
    int16_t lhsReg = setOutRegAbs(g, useImm ? dst : -1);
    int16_t rhsVal;
    if (useImm) {
        rhsVal = rhsNode->as.numInt;
    } else {
        if (!genExpr(g, node->as.binOp.rhs)) return;
        rhsVal = setOutRegAbs(g, dst);
    }

    releaseSlots(g, top);
    g->outReg = dst;
    if (!useOutRegNew(g, idx)) return;

    if (useImm)
        emitKs(g, slOpWithImmediate(op), g->outReg, lhsReg, rhsVal);
    else
        emitA(g, SlOp_add, g->outReg, lhsReg, rhsVal);
}

static void genNumInt(GenState *g, SlNodeIdx idx) {
    int64_t num = getNode(g, idx)->as.numInt;
    if (!useOutRegNew(g, idx)) return;
    if (num >= -128 && num <= 127) {
        emitIs(g, SlOp_ldi, g->outReg, (int32_t)num);
    } else {
        int32_t constIdx = addConst(g, idx, slObjInt(num));
        if (constIdx < 0) return;
        emitIu(g, SlOp_ldk, g->outReg, constIdx);
    }
}

static void genBoolLit(GenState *g, SlNodeIdx idx) {
    if (!useOutRegNew(g, idx)) return;
    emitIu(g, SlOp_ldv, g->outReg, getNode(g, idx)->as.boolLit);
}

static void genNullLit(GenState *g, SlNodeIdx idx) {
    if (!useOutRegNew(g, idx)) return;
    emitIu(g, SlOp_ldn, g->outReg, 1);
}

static int16_t findLocalVar(FuncState *f, SlStrIdx name, uint16_t *outShr) {
    assert(f != NULL);
    BlockState *block = f->block;
    while (block != NULL) {
        uint32_t *info = slStrMapGet(block->vars, name);
        if (info == NULL) {
            block = block->parent;
            continue;
        }
        if (outShr) *outShr = slVarShr(*info) + block->baseShr;
        return (int16_t)slVarIdx(*info) + block->baseReg;
    }
    return -1;
}

static int16_t findSharedVar(SlVM *vm, FuncState *f, SlStrIdx name) {
    assert(f != NULL);
    uint32_t *info = slStrMapGet(&f->externalVars, name);
    if (info != NULL) {
        return (int16_t)shrVarDst(*info);
    }

    assert(f->parent != NULL);
    uint16_t parentShr;
    bool fromShared = false;
    if (findLocalVar(f->parent, name, &parentShr) == -1) {
        fromShared = true;
        int16_t idx = findSharedVar(vm, f->parent, name);
        assert(idx != -1);
        parentShr = (uint16_t)idx;
    }

    uint32_t dstIdx = f->externalVars.len;
    assert((dstIdx & 0x7f) == dstIdx);
    uint32_t externalValue = (fromShared << 31) | (parentShr << 16) | dstIdx;
    if (!slStrMapSet(vm, &f->externalVars, name, externalValue)) return -1;
    return dstIdx;
}

static void genAccess(GenState *g, SlNodeIdx idx) {
    SlStrIdx name = getNode(g, idx)->as.access.name;
    bool local = getNode(g, idx)->as.access.local;

    int16_t varSlot;

    if (!local) {
        varSlot = findSharedVar(g->vm, g->func, name);
        if (varSlot == -1 || !useOutRegNew(g, idx)) return;
        emitIu(g, SlOp_ldsh, g->outReg, varSlot);
        return;
    }
    varSlot = findLocalVar(g->func, name, NULL);
    assert(varSlot != -1);
    if (g->outReg < 0) {
        g->outReg = varSlot;
    } else {
        emitT(g, SlOp_mov, g->outReg, varSlot);
    }
}

static void genAssign(GenState *g, SlNodeIdx idx) {
    int16_t varSlot;

    SlStrIdx name = getNode(g, idx)->as.assign.name;
    bool local = getNode(g, idx)->as.assign.local;
    SlNodeIdx value = getNode(g, idx)->as.assign.value;

    if (!local) {
        varSlot = findSharedVar(g->vm, g->func, name);
        if (varSlot == -1 || !genExpr(g, value)) return;
        emitIu(g, SlOp_stsh, g->outReg, varSlot);
        return;
    }
    varSlot = findLocalVar(g->func, name, NULL);
    assert(varSlot != -1);

    if (g->outReg >= 0) {
        int16_t oldOutReg = g->outReg;
        g->outReg = varSlot;
        if (!genExpr(g, value)) return;
        g->outReg = oldOutReg;
        emitT(g, SlOp_mov, g->outReg, varSlot);
    } else {
        g->outReg = varSlot;
        genExpr(g, value);
    }
}

static void genFuncCall(GenState *g, SlNodeIdx idx) {
    SlNode node = *getNode(g, idx);
    int16_t outReg = g->outReg;
    uint16_t firstSlot = getSlot(g);
    for (uint32_t i = 0; i < node.as.funcCall.nodeCount; i++) {
        g->outReg = getSlot(g);
        if (!useSlots(g, idx, 1)) return;
        if (!genExpr(g, node.as.funcCall.nodes[i])) return;
    }

    assert(getSlot(g) == firstSlot + node.as.funcCall.nodeCount);

    emitIu(g, SlOp_call, firstSlot, node.as.funcCall.nodeCount - 1);

    if (outReg == -1) {
        g->outReg = firstSlot;
        releaseSlots(g, firstSlot + 1);
    } else {
        emitT(g, SlOp_mov, outReg, firstSlot);
        releaseSlots(g, firstSlot);
    }
}

// BYTECODE PRINTING

static void printBytecode(const uint32_t *bytecode, uint32_t len) {
    enum OpType {
        OP_A,
        OP_KS,
        OP_KU,
        OP_IS,
        OP_IU,
        OP_T,
        OP_O,
        OP_J
    };

    // for (uint32_t i = 0; i < len; i++) {
    //     printf("%4"PRIu32"  0x%08"PRIx32"\n", i, bytecode[i]);
    // }

    for (uint32_t i = 0; i < len;) {
        printf("%4"PRIu32"  ",  i);
        uint32_t op = bytecode[i++];
        uint32_t ex = 0;
        if (op & 1) ex = bytecode[i++];

        enum OpType type;
        switch ((SlOpCode)(op >> 1 & 0x7f)) {
        case SlOp_add:   printf("add");   type = OP_A;  break;
        case SlOp_addi:  printf("addi");  type = OP_KS; break;
        case SlOp_sub:   printf("sub");   type = OP_A;  break;
        case SlOp_subi:  printf("subi");  type = OP_KS; break;
        case SlOp_mul:   printf("mul");   type = OP_A;  break;
        case SlOp_muli:  printf("muli");  type = OP_KS; break;
        case SlOp_div:   printf("div");   type = OP_A;  break;
        case SlOp_divi:  printf("divi");  type = OP_KS; break;
        case SlOp_mod:   printf("mod");   type = OP_A;  break;
        case SlOp_modi:  printf("modi");  type = OP_KS; break;
        case SlOp_pow:   printf("pow");   type = OP_A;  break;
        case SlOp_powi:  printf("powi");  type = OP_KS; break;
        case SlOp_eq:    printf("eq");    type = OP_A;  break;
        case SlOp_eqi:   printf("eqi");   type = OP_KS; break;
        case SlOp_ne:    printf("ne");    type = OP_A;  break;
        case SlOp_nei:   printf("nei");   type = OP_KS; break;
        case SlOp_lt:    printf("lt");    type = OP_A;  break;
        case SlOp_lti:   printf("lti");   type = OP_KS; break;
        case SlOp_le:    printf("le");    type = OP_A;  break;
        case SlOp_lei:   printf("lei");   type = OP_KS; break;
        case SlOp_gt:    printf("gt");    type = OP_A;  break;
        case SlOp_gti:   printf("gti");   type = OP_KS; break;
        case SlOp_ge:    printf("ge");    type = OP_A;  break;
        case SlOp_gei:   printf("gei");   type = OP_KS; break;
        case SlOp_mov:   printf("mov");   type = OP_T;  break;
        case SlOp_ldn:   printf("ldn");   type = OP_IU; break;
        case SlOp_ldi:   printf("ldi");   type = OP_IS; break;
        case SlOp_ldv:   printf("ldv");   type = OP_IU; break;
        case SlOp_ldk:   printf("ldk");   type = OP_IU; break;
        case SlOp_ldsh:  printf("ldsh");  type = OP_IU; break;
        case SlOp_stsh:  printf("stsh");  type = OP_IU; break;
        case SlOp_mksh:  printf("mksh");  type = OP_T;  break;
        case SlOp_dtsh:  printf("dtsh");  type = OP_IU; break;
        case SlOp_mkf:   printf("mkf");   type = OP_IU; break;
        case SlOp_call:  printf("call");  type = OP_IU; break;
        case SlOp_tcall: printf("tcall"); type = OP_IU; break;
        case SlOp_ret:   printf("ret");   type = OP_O;  break;
        case SlOp_retv:  printf("retv");  type = OP_IU; break;
        case SlOp_jmp:   printf("jmp");   type = OP_J;  break;
        case SlOp_teq:   printf("teq");   type = OP_T;  break;
        case SlOp_teqi:  printf("teqi");  type = OP_IS; break;
        case SlOp_tne:   printf("tne");   type = OP_T;  break;
        case SlOp_tnei:  printf("tnei");  type = OP_IS; break;
        case SlOp_tlt:   printf("tlt");   type = OP_T;  break;
        case SlOp_tlti:  printf("tlti");  type = OP_IS; break;
        case SlOp_tle:   printf("tle");   type = OP_T;  break;
        case SlOp_tlei:  printf("tlei");  type = OP_IS; break;
        case SlOp_tgt:   printf("tgt");   type = OP_T;  break;
        case SlOp_tgti:  printf("tgti");  type = OP_IS; break;
        case SlOp_tge:   printf("tge");   type = OP_T;  break;
        case SlOp_tgei:  printf("tgei");  type = OP_IS; break;
        case SlOp_ttr:   printf("ttr");   type = OP_O;  break;
        case SlOp_tfl:   printf("tfl");   type = OP_O;  break;
        case SlOp_tnl:   printf("tnl");   type = OP_O;  break;
        case SlOp_tnnl:  printf("tnnl");  type = OP_O;  break;
        case SlOp_cget:  printf("cget");  type = OP_A;  break;
        case SlOp_cgeti: printf("cgeti"); type = OP_KS; break;
        case SlOp_cgetk: printf("cgetk"); type = OP_KU; break;
        case SlOp_cset:  printf("cset");  type = OP_A;  break;
        case SlOp_cseti: printf("cseti"); type = OP_KS; break;
        case SlOp_csetk: printf("csetk"); type = OP_KU; break;
        case SlOp_print: printf("print"); type = OP_O;  break;
        }

        switch (type) {
        case OP_A: {
            uint16_t rd = (ex & 0xff00) | (op >> 8 & 0xff);
            uint16_t r1 = (ex >> 8 & 0xff00) | (op >> 16 & 0xff);
            uint16_t r2 = (ex >> 16 & 0xff00) | (op >> 24 & 0xff);
            printf("\tr%u\tr%u\tr%u", rd, r1, r2);
            break;
        }
        case OP_KU: {
            uint16_t rd = (ex & 0xff00) | (op >> 8 & 0xff);
            uint16_t r1 = (ex >> 8 & 0xff00) | (op >> 16 & 0xff);
            uint16_t imm = (ex >> 16 & 0xff00) | (op >> 24 & 0xff);
            printf("\tr%u\tr%u\t%u", rd, r1, imm);
            break;
        }
        case OP_KS: {
            uint16_t rd = (ex & 0xff00) | (op >> 8 & 0xff);
            uint16_t r1 = (ex >> 8 & 0xff00) | (op >> 16 & 0xff);
            int16_t imm;
            if (ex == 0)
                imm = (int8_t)(op >> 24 & 0xff);
            else
                imm = (int16_t)((ex >> 16 & 0xff00) | (op >> 24 & 0xff));
            printf("\tr%u\tr%u\t%+d", rd, r1, imm);
            break;
        }
        case OP_IU: {
            uint16_t rd = op >> 8 & 0xffff;
            uint32_t imm = (ex >> 16 & 0xffffff00) | (op >> 24 & 0xff);
            printf("\tr%u\t%"PRIu32, rd, imm);
            break;
        }
        case OP_IS: {
            uint16_t rd = op >> 8 & 0xffff;
            int32_t imm;
            if (ex == 0)
                imm = (int8_t)(op >> 24 & 0xff);
            else
                imm = (int32_t)((ex >> 16 & 0xffffff00) | (op >> 24 & 0xff));
            printf("\tr%u\t%+"PRIi32, rd, imm);
            break;
        }
        case OP_T: {
            uint16_t rd = (ex & 0xff00) | (op >> 8 & 0xff);
            uint16_t r1 = op >> 16 & 0xffff;
            printf("\tr%u\tr%u", rd, r1);
            break;
        }
        case OP_O: {
            uint16_t rd = op >> 8 & 0xffff;
            printf("\tr%u", rd);
            break;
        }
        case OP_J: {
            int32_t imm;
            if (op >> 31)
                imm = 0xff000000 | op >> 8;
            else
                imm = op >> 8;
            printf("\t%+"PRIi32" (to %"PRIi32")", imm, i + imm);
            break;
        }
        }
        printf("\n");
        if (ex != 0)
            printf("%4"PRIu32"  --- ext ---\n",  i - 1);
    }
}

void printPrototype(SlObj main) {
    Constants toPrint = { 0 };
    SlVM dummy = { 0 };
    if (!constsPush(&dummy, &toPrint, main)) return;

    for (uint32_t i = 0; i < toPrint.len; i++) {
        assert(toPrint.data[i].type == SlObj_Prototype);
        SlPrototype *proto = toPrint.data[i].as.proto;
        printf("<%p> bytecode:\n", (void *)proto);
        printBytecode(proto->bytecode, proto->size);
        if (proto->sharedCount == 0) goto printConstants;

        printf("----shared info:\n");
        for (uint32_t j = 0; j < proto->sharedCount; j++) {
            SlSharedInfo info = proto->sharedInfo[j];
            printf(
                "\t[%u] %"PRIu16" (%s)\n",
                j,
                info.idx,
                info.fromShared ? "shr" : "stack"
            );
        }

    printConstants:
        if (proto->constCount == 0) continue;
        printf("----constants:\n");
        for (uint32_t j = 0; j < proto->constCount; j++) {
            SlObj obj = proto->constants[j];
            printf("\t[%u] (%s)", j, slTypeName(obj));
            switch (obj.type) {
            case SlObj_Int:
                printf(" %"PRIi64, obj.as.numInt);
                break;
            case SlObj_Prototype:
                printf(" <%p>", (void *)obj.as.proto);
                if (!constsPush(&dummy, &toPrint, obj)) return;
                break;
            default:
                break;
            }
            printf("\n");
        }
    }
}
