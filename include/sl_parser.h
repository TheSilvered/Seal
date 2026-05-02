#ifndef SL_PARSER_H_
#define SL_PARSER_H_

#include <stdint.h>
#include "sl_vm.h"
#include "sl_hashmap.h"

typedef enum SlNodeKind {
    SlNode_INVALID,

    SlNode_Block,
    SlNode_VarDeclr,
    SlNode_IfStmnt,
    SlNode_WhileLoop,
    SlNode_Print,
    SlNode_RetStmnt,

    SlNode_BinOp,
    SlNode_NumInt,
    SlNode_BoolLit,
    SlNode_NullLit,
    SlNode_Access,
    SlNode_Assign,
    SlNode_Lambda
} SlNodeKind;

typedef enum SlBinOp {
    SlBinOp_Add,
    SlBinOp_Sub,
    SlBinOp_Mul,
    SlBinOp_Div,
    SlBinOp_Mod,
    SlBinOp_Pow,

    SlBinOp_Lt, // NOTE: keep in sync with isOpJumpable in codegen
    SlBinOp_Le,
    SlBinOp_Gt,
    SlBinOp_Ge,
    SlBinOp_Eq,
    SlBinOp_Ne
    // NOTE: do not add new operations at the end
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
        struct {
            SlStrIdx name;
            bool local;
        } access;
        struct {
            SlStrIdx name;
            SlNodeIdx value;
            bool local;
        } assign;
        struct {
            SlNodeIdx *nodes;
            SlStrMap *vars;
            uint16_t funcCount;
            uint16_t sharedCount;
            uint32_t nodeCount;
        } block;
        struct {
            SlNodeIdx lhs, rhs;
            SlBinOp op;
        } binOp;
        // if there are any parameters, `body` references a block node which
        // binds the parameters and contains exactly one node that is the body
        // of the function
        // if there are no parameters `body` is the actual body
        struct {
            uint16_t paramCount;
            SlNodeIdx body;
        } lambda;
        struct {
            SlNodeIdx condition;
            SlNodeIdx ifTrue;
            SlNodeIdx ifFalse;
        } ifStmnt;
        struct {
            SlNodeIdx condition;
            SlNodeIdx body;
        } whileLoop;
        SlNodeIdx retStmnt;
        SlNodeIdx print;
        int64_t numInt;
        bool boolLit;
    } as;
} SlNode;

typedef struct SlAst {
    uint8_t *strs;
    SlNode *nodes;
    uint32_t nodeCount;
    SlNodeIdx root;
} SlAst;

SlAst slParse(SlVM *vm, const SlSource *source);
void slDestroyAst(SlAst *ast);
void slPrintAst(const SlAst *ast);

#endif // !SL_PARSER_H_
