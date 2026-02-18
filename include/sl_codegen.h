#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_parser.h"
#include "sl_object.h"

// Format:
// parameter_name(number_of_bytes)
// r = register, k = constant

typedef enum SlOpCode {
    SlOp_nop,
    SlOp_ldi8, // dest(r), value(1)
    SlOp_ldk,  // dest(r), source(k)
    SlOp_cpy,  // dest(r), source(r)
    SlOp_add,  // dest(r), lhs(r), rhs(r)
    SlOp_sub,  // dest(r), lhs(r), rhs(r)
    SlOp_mul,  // dest(r), lhs(r), rhs(r)
    SlOp_div,  // dest(r), lhs(r), rhs(r)
    SlOp_mod,  // dest(r), lhs(r), rhs(r)
    SlOp_pow,  // dest(r), lhs(r), rhs(r)
} SlOpCode;

SlObj slGenCode(SlVM *vm, SlSource *source);

#endif // !SL_CODEGEN_H_
