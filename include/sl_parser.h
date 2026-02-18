#ifndef SL_PARSER_H_
#define SL_PARSER_H_

#include <stdint.h>
#include "sl_lexer.h"
#include "sl_vm.h"

typedef enum SlNodeKind {
    SlNode_Block,
    SlNode_VarDeclr,
    SlNode_BinOp,
    SlNode_NumInt,
    SlNode_Access
} SlNodeKind;

typedef enum SlBinOp {
    SlBinOp_Add,
    SlBinOp_Sub,
    SlBinOp_Mul,
    SlBinOp_Div,
    SlBinOp_Mod,
    SlBinOp_Pow
} SlBinOp;

typedef int32_t SlNodeIdx;

typedef struct SlNode {
    SlNodeKind kind;
    uint32_t line;
    union {
        struct {
            SlStrIdx name;
            SlNodeIdx value;
        } varDeclr;
        SlStrIdx access;
        struct {
            SlNodeIdx *nodes;
            uint32_t nodeCount;
        } block;
        struct {
            SlNodeIdx lhs, rhs;
            SlBinOp op;
        } binOp;
        int64_t numInt;
    } as;
} SlNode;

typedef struct SlAst {
    uint8_t *strs;
    SlNode *nodes;
    uint32_t nodeCount;
    SlNodeIdx root;
} SlAst;

SlAst slParse(SlVM *vm, SlSource *source);
void slPrintAst(const SlAst *ast);

#endif // !SL_PARSER_H_
