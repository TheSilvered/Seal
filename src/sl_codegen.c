#include "sl_array.h"
#include "sl_codegen.h"
#include "sl_parser.h"

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
} SlGenState;

static bool genNode(SlGenState *g, SlNodeIdx nodeIdx);
static bool genBlock(SlGenState *g, SlNodeIdx nodeIdx);
static bool genVarDeclr(SlGenState *g, SlNodeIdx nodeIdx);

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

static bool genNode(SlGenState *g, SlNodeIdx nodeIdx) {
    switch (g->ast.nodes[nodeIdx].kind) {
    case SlNode_Block:
        return genBlock(g, nodeIdx);
    case SlNode_VarDeclr:
        return genVarDeclr(g, nodeIdx);
    }
}

static bool genBlock(SlGenState *g, SlNodeIdx nodeIdx) {
    SlNode node = g->ast.nodes[nodeIdx];
    for (uint32_t i = 0; i < node.as.block.nodeCount; i++) {
        if (!genNode(g, node.as.block.nodes[i])) {
            return false;
        }
    }
    return true;
}
