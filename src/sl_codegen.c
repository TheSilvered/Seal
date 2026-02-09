#include "sl_array.h"
#include "sl_codegen.h"
#include "sl_parser.h"
#include "sl_object.h"

#define _maxSlots ((1 << 15) + 0x80 - 1)

slArrayType(SlObj, Constants, consts)
slArrayImpl(SlObj, Constants, consts)

typedef struct SlVarName {
    uint32_t idx, len;
} SlVarName;

slArrayType(SlVarName, Variables, vars)
slArrayImpl(SlVarName, Variables, vars)

typedef struct SlGenState {
    SlVM *vm;
    u8Arr bytecode;
    Constants consts;
    Variables vars;
    SlAst ast;
    uint16_t outReg;
    uint16_t usedRegCount;
} SlGenState;

static bool genNode(SlGenState *g, SlNodeIdx nodeIdx);
static bool genBlock(SlGenState *g, SlNodeIdx nodeIdx);
static bool genVarDeclr(SlGenState *g, SlNodeIdx nodeIdx);
static bool genBinOp(SlGenState *g, SlNodeIdx nodeIdx);
static bool genNumInt(SlGenState *g, SlNodeIdx nodeIdx);

static bool pushOp(SlGenState *g, SlOpCode opCode);
static bool pushByte(SlGenState *g, uint8_t byte);
static bool pushReg(SlGenState *g, uint16_t reg);
static bool push32(SlGenState *g, uint32_t reg);

static int64_t addConst(SlGenState *g, SlObj obj);

static uint32_t regCount(SlGenState *g, SlNodeIdx nodeIdx);

SlObj slGenCode(SlVM *vm, SlSource *source) {
    SlGenState g = {
        .vm = vm,
        .bytecode = { 0 },
        .consts = { 0 },
        .vars = { 0 },
    };
    g.ast = slParse(vm, source);
    if (vm->error.occurred) {
        return slNull;
    }

    if (!genNode(&g, g.ast.root)) {
        return slNull;
    }
}

static bool pushOp(SlGenState *g, SlOpCode opCode) {
    return pushByte(g, (uint8_t)opCode);
}

static bool pushByte(SlGenState *g, uint8_t byte) {
    if (!u8Push(&g->bytecode, byte)) {
        slSetOutOfMemoryError(g->vm);
        return false;
    } else {
        return true;
    }
}

static bool pushReg(SlGenState *g, uint16_t reg) {
    if (reg > _maxSlots) {
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

static bool push32(SlGenState *g, uint32_t n) {
    return pushByte(g, (n >> 24) & 0xff)
        && pushByte(g, (n >> 16) & 0xff)
        && pushByte(g, (n >>  8) & 0xff)
        && pushByte(g,  n        & 0xff);
}

static int64_t addConst(SlGenState *g, SlObj obj) {
    if (!constsPush(&g->consts, obj)) {
        slSetOutOfMemoryError(g->vm);
        return -1;
    }
    return g->consts.len - 1;
}

static bool genNode(SlGenState *g, SlNodeIdx nodeIdx) {
    switch (g->ast.nodes[nodeIdx].kind) {
    case SlNode_Block:
        return genBlock(g, nodeIdx);
    case SlNode_VarDeclr:
        return genVarDeclr(g, nodeIdx);
    case SlNode_BinOp:
        return genBinOp(g, nodeIdx);
    case SlNode_NumInt:
        return genNumInt(g, nodeIdx);
    }
}

static bool genBlock(SlGenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    g->usedRegCount += node.as.block.varCount;

    for (uint32_t i = 0; i < node.as.block.nodeCount; i++) {
        if (!genNode(g, node.as.block.nodes[i])) {
            return false;
        }
    }

    g->usedRegCount -= node.as.block.varCount;
    return true;
}

static bool genVarDeclr(SlGenState *g, SlNodeIdx nodeIdx) {
    
}

static bool genBinOp(SlGenState *g, SlNodeIdx nodeIdx) {
    
}

static bool genNumInt(SlGenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    if (node.as.numInt < 128 && node.as.numInt >= -128) {
        uint8_t byte = (uint8_t)((int8_t)node.as.numInt);
        return pushOp(g, SlOp_ldi8)
            && pushReg(g, g->outReg)
            && pushByte(g, byte);
    }
    int64_t constIdx = addConst(g, slObjInt(node.as.numInt));
    if (constIdx < 0) {
        return false;
    }
    return pushOp(g, SlOp_ldk)
        && pushReg(g, g->outReg)
        && push32(g, (uint32_t)constIdx);
}

static uint32_t regCount(SlGenState *g, SlNodeIdx nodeIdx) {
    SlNode *node = &g->ast.nodes[nodeIdx];
    switch (g->ast.nodes[nodeIdx].kind) {
    case SlNode_Access:
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
        assert(("node is not an expression", false));
    }
}
