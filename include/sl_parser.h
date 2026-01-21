#ifndef SL_PARSER_H_
#define SL_PARSER_H_

#include <stdint.h>
#include "sl_lexer.h"
#include "sl_vm.h"

typedef enum SlNodeKind {
    SlNode_Block,
    SlNode_VarDeclr,
    SlNode_Expr
} SlNodeKind;

typedef uint32_t SlNodeIdx;

typedef struct SlNode {
    SlNodeKind kind;
    union {
        struct {
            uint32_t nameIdx, nameLen;
            SlNodeIdx value;
        } varDeclr;
    } as;
} SlNode;

typedef struct SlAst {
    uint8_t *strs;
} SlAst;

SlAst slParse(SlVM *vm, const uint8_t *text, uint32_t len);

#endif // !SL_PARSER_H_
