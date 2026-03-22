#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_parser.h"
#include "sl_vm.h"

// Comma separated argument list:
// name(format)
// Possible formats are:
// - r: register-like (1-byte for 0 to 127, two bytes above)
// - s: signed byte
// - U: unsigned 32-bit integer (saved in big endian)

typedef enum SlOpCode {
    SlOp_nop,

    SlOp_ldnull, // dest(r)
    SlOp_ldi8, // dest(r), value(s)
    SlOp_ldk,  // dest(r), source(U)
    SlOp_cpy,  // dest(r), source(r)

    SlOp_add,  // dest(r), lhs(r), rhs(r)
    SlOp_sub,  // dest(r), lhs(r), rhs(r)
    SlOp_mul,  // dest(r), lhs(r), rhs(r)
    SlOp_div,  // dest(r), lhs(r), rhs(r)
    SlOp_mod,  // dest(r), lhs(r), rhs(r)
    SlOp_pow,  // dest(r), lhs(r), rhs(r)

    SlOp_print, // value(r)
    SlOp_ret,   // value(r)
} SlOpCode;

SlObj slGenCode(SlVM *vm, SlSource *source);

#endif // !SL_CODEGEN_H_
