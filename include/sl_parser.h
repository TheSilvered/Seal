#ifndef SL_PARSER_H_
#define SL_PARSER_H_

#include <stdint.h>
#include "sl_lexer.h"
#include "sl_vm.h"

typedef enum SlNodeKind {
    SlNode_INVALID,

    SlNode_Block,
    SlNode_VarDeclr,
    SlNode_FuncDeclr,
    SlNode_BinOp,
    SlNode_NumInt,
    SlNode_Access,
    SlNode_Print,
    SlNode_Lambda,
    SlNode_RetStmnt
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
typedef struct SlUniqueVar {
    SlStrIdx name;
    uint32_t id;
} SlUniqueVar;

typedef struct SlNode {
    SlNodeKind kind;
    uint32_t line;
    union {
        struct {
            SlStrIdx name;
            uint32_t id;
            SlNodeIdx value;
        } varDeclr, funcDeclr;
        SlUniqueVar access;
        struct {
            SlNodeIdx *nodes;
            uint32_t nodeCount;
        } block;
        struct {
            SlNodeIdx lhs, rhs;
            SlBinOp op;
        } binOp;
        struct {
            SlUniqueVar *vars; // [params | shared vars]
            uint16_t paramCount;
            uint16_t sharedCount;
            SlNodeIdx body;
        } lambda;
        SlNodeIdx retStmnt;
        SlNodeIdx print;
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
void slDestroyAst(SlAst *ast);
void slPrintAst(const SlAst *ast);

#endif // !SL_PARSER_H_
