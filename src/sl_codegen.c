#include <string.h>

#include "sl_array.h"
#include "sl_codegen.h"

#define _maxRegs ((1 << 15) + 0x80 - 1)
#define _strFmt(str) (int)(str).len, (char *)(g->ast.strs + (str).idx)

slArrayType(SlObj, Constants, consts)
slArrayImpl(SlObj, Constants, consts)

typedef struct VarReg {
    SlStrIdx name;
    uint16_t reg;
} VarReg;

slArrayType(VarReg, Variables, vars)
slArrayImpl(VarReg, Variables, vars)

typedef struct VarTable {
    struct VarTable *parent;
    Variables vars;
} VarTable;

typedef struct GenState {
    SlVM *vm;
    u8Arr bytecode;
    Constants consts;
    VarTable *varsTop;
    SlAst ast;
    int32_t outReg;
    uint16_t usedStack;
    uint16_t maxStackSize;
} GenState;

static uint16_t getFreeReg(GenState *g);

static bool pushOp(GenState *g, SlOpCode opCode);
static bool pushByte(GenState *g, uint8_t byte);
static bool pushReg(GenState *g, int32_t reg);
static bool pushU32(GenState *g, uint32_t reg);

static bool pushVarTable(GenState *g);
static void popVarTable(GenState *g);
static bool addVar(GenState *g, uint16_t reg, SlStrIdx var);
static int32_t getVar(GenState *g, SlStrIdx var);
static int64_t addConst(GenState *g, SlObj obj);
static uint32_t regCount(GenState *g, SlNodeIdx nodeIdx);

static bool genStatement(GenState *g, SlNodeIdx nodeIdx);
static bool genBlock(GenState *g, SlNodeIdx nodeIdx);
static bool genVarDeclr(GenState *g, SlNodeIdx nodeIdx);
static int32_t genExpr(GenState *g, SlNodeIdx nodeIdx);
static int32_t genBinOp(GenState *g, SlNodeIdx nodeIdx);
static int32_t genNumInt(GenState *g, SlNodeIdx nodeIdx);
static int32_t genAccess(GenState *g, SlNodeIdx nodeIdx);

// TEMPORARY PRINTING

static void printReg(GenState *g, uint32_t *i) {
    uint16_t b1 = g->bytecode.data[++*i];
    if (b1 < 0x80) {
        printf("%u", b1);
        return;
    }
    uint16_t b2 = g->bytecode.data[++*i];
    printf("%u", (((b1 & 0x7F) << 8) | b2) + 0x80);
}

static void printU32(GenState *g, uint32_t *i) {
    uint32_t val = 0;
    for (int j = 0; j < 4; j++) {
        val <<= 8;
        val |= g->bytecode.data[++*i];
    }
    printf("%"PRId32, (int32_t)val);
}

static void printInst(
    GenState *g,
    uint32_t *i,
    const char *name,
    const char *args
) {
    printf("%s", name);
    while (*args) {
        char c = *args++;
        putchar('\t');
        switch (c) {
        case 'r':
            printReg(g, i);
            break;
        case 'b':
            printf("%"PRIx8, g->bytecode.data[++*i]);
            break;
        case 'i':
            printU32(g, i);
            break;
        default:
            assert(false && "bad args format");
        }
    }
    putchar('\n');
}

static void printBytecode(GenState *g) {
    u8Arr bytecode = g->bytecode;
    for (uint32_t i = 0; i < bytecode.len; i++) {
        switch (bytecode.data[i]) {
        case SlOp_nop:
            printInst(g, &i, "nop", "");
            break;
        case SlOp_ldi8:
            printInst(g, &i, "ldi8", "rb");
            break;
        case SlOp_ldk:
            printInst(g, &i, "ld", "ri");
            break;
        case SlOp_cpy:
            printInst(g, &i, "cpy", "rr");
            break;
        case SlOp_add:
            printInst(g, &i, "add", "rrr");
            break;
        case SlOp_sub:
            printInst(g, &i, "sub", "rrr");
            break;
        case SlOp_mul:
            printInst(g, &i, "mul", "rrr");
            break;
        case SlOp_div:
            printInst(g, &i, "div", "rrr");
            break;
        case SlOp_mod:
            printInst(g, &i, "mod", "rrr");
            break;
        case SlOp_pow:
            printInst(g, &i, "pow", "rrr");
            break;
        }
    }
}

// ==================

SlObj slGenCode(SlVM *vm, SlSource *source) {
    SlObj result = slNull;

    GenState g = {
        .vm = vm,
        .bytecode = { 0 },
        .consts = { 0 },
        .varsTop = NULL,
        .outReg = -1,
        .usedStack = 0,
        .maxStackSize = 0
    };
    g.ast = slParse(vm, source);
    if (vm->error.occurred) {
        goto exit;
    }

    if (!genStatement(&g, g.ast.root)) {
        goto exit;
    }

    SlBytecode *bc = slBytecodeNew(
        vm,
        g.bytecode.data,
        g.bytecode.len,
        g.maxStackSize,
        g.consts.data,
        g.consts.len,
        NULL
    );
    if (bc == NULL) {
        goto exit;
    }

    SlStr *funcName = slFrozenStrNew(
        vm,
        (uint8_t *)source->path,
        strlen(source->path)
    );
    if (funcName == NULL) {
        goto exit;
    }

    SlFunc *mainFunc = slFrozenFuncNew(vm, funcName, bc);
    if (mainFunc == NULL) {
        goto exit;
    }

    result = (SlObj){
        .type = SlObj_FrozenFunc,
        .as.func = mainFunc
    };

exit:
    u8Clear(&g.bytecode);
    constsClear(&g.consts);
    while (g.varsTop != NULL) {
        popVarTable(&g);
    }

    return result;
}

static uint16_t getFreeReg(GenState *g) {
    uint16_t reg = g->usedStack++;
    if (g->usedStack > g->maxStackSize) {
        g->maxStackSize = g->usedStack;
    }
    return reg;
}

static bool pushOp(GenState *g, SlOpCode opCode) {
    return pushByte(g, (uint8_t)opCode);
}

static bool pushByte(GenState *g, uint8_t byte) {
    if (!u8Push(&g->bytecode, byte)) {
        slSetOutOfMemoryError(g->vm);
        return false;
    } else {
        return true;
    }
}

static bool pushReg(GenState *g, int32_t reg) {
    // fewer type conversions with int32_t but a valid uint16_t is expected
    assert(reg >= 0 && reg <= UINT16_MAX);
    if (reg > _maxRegs) {
        slSetError(
            g->vm,
            "function register limit exceeded (%d registers)",
            reg
        );
        return false;
    }

    if (reg < 0x80) {
        return pushByte(g, (uint8_t)reg);
    } else {
        reg -= 0x80;
        return pushByte(g, 0x80 | (reg >> 8)) && pushByte(g, reg & 0xff);
    }
}

static bool pushU32(GenState *g, uint32_t n) {
    return pushByte(g, (n >> 24) & 0xff)
        && pushByte(g, (n >> 16) & 0xff)
        && pushByte(g, (n >>  8) & 0xff)
        && pushByte(g,  n        & 0xff);
}

static bool pushVarTable(GenState *g) {
    VarTable *table = memAllocZeroed(1, sizeof(*table));
    if (table == NULL) {
        slSetOutOfMemoryError(g->vm);
        return false;
    }
    table->parent = g->varsTop;
    g->varsTop = table;
    return true;
}

static void popVarTable(GenState *g) {
    assert(g->varsTop != NULL);
    VarTable *parent = g->varsTop->parent;
    varsClear(&g->varsTop->vars);
    memFree(g->varsTop);
    g->varsTop = parent;
}

static bool strsEq(GenState *g, SlStrIdx str1, SlStrIdx str2) {
    return str1.len == str2.len
        && 0 == memcmp(
            g->ast.strs + str1.idx,
            g->ast.strs + str2.idx,
            str1.len
        );
}

static bool addVar(GenState *g, uint16_t reg, SlStrIdx var) {
    Variables *vars = &g->varsTop->vars;
    for (uint32_t i = 0; i < g->varsTop->vars.len; i++) {
        if (strsEq(g, vars->data[i].name, var)) {
            slSetError(g->vm, "duplicate declaration of '%.*s'", _strFmt(var));
            return false;
        }
    }
    if (!varsPush(vars, (VarReg){ .name = var, .reg = reg })) {
        slSetOutOfMemoryError(g->vm);
        return false;
    }
    return true;
}

static int32_t getVar(GenState *g, SlStrIdx var) {
    VarTable *table = g->varsTop;
    while (table != NULL) {
        Variables *vars = &g->varsTop->vars;
        for (uint32_t i = 0; i < g->varsTop->vars.len; i++) {
            if (strsEq(g, vars->data[i].name, var)) {
                return vars->data[i].reg;
            }
        }
        table = table->parent;
    }
    slSetError(g->vm, "unknown variable '%.*s'", _strFmt(var));
    return -1;
}

static int64_t addConst(GenState *g, SlObj obj) {
    if (!constsPush(&g->consts, obj)) {
        slSetOutOfMemoryError(g->vm);
        return -1;
    }
    return g->consts.len - 1;
}

static uint32_t regCount(GenState *g, SlNodeIdx nodeIdx) {
    SlNode *node = &g->ast.nodes[nodeIdx];
    switch (g->ast.nodes[nodeIdx].kind) {
    case SlNode_Access:
        return 0;
    case SlNode_NumInt:
        return 1;
    case SlNode_BinOp: {
        uint32_t lCount = regCount(g, node->as.binOp.lhs);
        uint32_t rCount = regCount(g, node->as.binOp.rhs);
        if (lCount == rCount) {
            return lCount + 1;
        } else if (lCount > rCount) {
            return lCount;
        } else {
            return rCount;
        }
    }
    default:
        assert(false && "node is not an expression");
    }
    return 0;
}

static int32_t genExpr(GenState *g, SlNodeIdx nodeIdx) {
    switch (g->ast.nodes[nodeIdx].kind) {
    case SlNode_BinOp:
        return genBinOp(g, nodeIdx);
    case SlNode_NumInt:
        return genNumInt(g, nodeIdx);
    case SlNode_Access:
        return genAccess(g, nodeIdx);
    default:
        assert(false && "node is not an expression");
    }
    return -1;
}

static bool genStatement(GenState *g, SlNodeIdx nodeIdx) {
    g->outReg = -1;
    switch (g->ast.nodes[nodeIdx].kind) {
    case SlNode_Block:
        return genBlock(g, nodeIdx);
    case SlNode_VarDeclr:
        return genVarDeclr(g, nodeIdx);
    case SlNode_NumInt:
        return true;
    case SlNode_Access:
        return getVar(g, g->ast.nodes[nodeIdx].as.access) != -1;
    case SlNode_BinOp:
        if (genBinOp(g, nodeIdx) == -1) {
            return false;
        }
        g->usedStack--;
        return true;
    default:
        assert(false && "unhandled note kind");
        return false;
    }
}

static bool genBlock(GenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    uint16_t initialStack = g->usedStack;
    if (!pushVarTable(g)) {
        return false;
    }

    for (uint32_t i = 0; i < node.as.block.nodeCount; i++) {
        if (!genStatement(g, node.as.block.nodes[i])) {
            return false;
        }
    }

    g->usedStack = initialStack;
    popVarTable(g);
    return true;
}

static bool genVarDeclr(GenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    uint16_t reg = getFreeReg(g);

    g->outReg = reg;
    if (genExpr(g, node.as.varDeclr.value) == -1) {
        return false;
    }

    // add after genExpr otherwise 'a' can be accessed
    return addVar(g, reg, node.as.varDeclr.name);
}

static int32_t genBinOp(GenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    int32_t outReg = g->outReg;
    uint16_t initialStack = g->usedStack;
    g->outReg = -1;

    // TODO: generate the expression with the higher register count first

    int32_t lhs = genExpr(g, node.as.binOp.lhs);
    if (lhs == -1) {
        return -1;
    }
    int32_t rhs = genExpr(g, node.as.binOp.rhs);
    if (rhs == -1) {
        return -1;
    }
    g->usedStack = initialStack;

    if (outReg == -1) {
        outReg = getFreeReg(g);
    }

    bool result = false;
    switch (node.as.binOp.op) {
    case SlBinOp_Add:
        result = pushOp(g, SlOp_add);
        break;
    case SlBinOp_Sub:
        result = pushOp(g, SlOp_sub);
        break;
    case SlBinOp_Mul:
        result = pushOp(g, SlOp_mul);
        break;
    case SlBinOp_Div:
        result = pushOp(g, SlOp_div);
        break;
    case SlBinOp_Mod:
        result = pushOp(g, SlOp_mod);
        break;
    case SlBinOp_Pow:
        result = pushOp(g, SlOp_pow);
        break;
    default:
        assert(false && "unhandled binary operation");
    }

    if (result && pushReg(g, outReg) && pushReg(g, lhs) && pushReg(g, rhs)) {
        return outReg;
    } else {
        return -1;
    }
}

static int32_t genNumInt(GenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    uint16_t outReg = g->outReg == -1 ? getFreeReg(g) : g->outReg;
    if (node.as.numInt < 128 && node.as.numInt >= -128) {
        uint8_t byte = (uint8_t)((int8_t)node.as.numInt);
        if (pushOp(g, SlOp_ldi8) && pushReg(g, outReg) && pushByte(g, byte)) {
            return outReg;
        } else {
            return -1;
        }
    }
    int64_t constIdx = addConst(g, slObjInt(node.as.numInt));
    if (constIdx < 0) {
        return false;
    }
    if (
        pushOp(g, SlOp_ldk)
        && pushReg(g, outReg)
        && pushU32(g, (uint32_t)constIdx)
    ) {
        return outReg;
    } else {
        return -1;
    }
}

static int32_t genAccess(GenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    int32_t varReg = getVar(g, node.as.access);
    if (varReg == -1 || g->outReg == -1 || varReg == g->outReg) {
        return varReg;
    }

    if (pushOp(g, SlOp_cpy) && pushReg(g, g->outReg) && pushReg(g, varReg)) {
        return g->outReg;
    } else {
        return -1;
    }
}
