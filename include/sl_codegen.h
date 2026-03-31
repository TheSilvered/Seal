#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_parser.h"
#include "sl_vm.h"

// Comma separated argument list:
// name.format
// Possible formats are:
// - r: register-like (1-byte for 0 to 127, two bytes above)
// - B: signed byte
// - b: unsigned byte
// - S: signed 16-bit integer (saved in big endian)
// - s: unsigned 16-bit integer (saved in big endian)
// - I: signed 32-bit integer (saved in big endian)
// - i: unsigned 32-bit integer (saved in big endian)

typedef enum SlOpCode {
    SlOp_nop, // no operation

    SlOp_ldn, // dst.r; load null: stack[dst] = null
    SlOp_ldi8,// dst.r val.B; load int8_t: stack[dst] = int(val)
    SlOp_ldkb,// dst.r src.b; load constant byte:  stack[dst] = constants[src]
    SlOp_ldks,// dst.r src.s; load constant short: stack[dst] = constants[src]
    SlOp_ldki,// dst.r src.i; load constant int:   stack[dst] = constants[src]
    SlOp_cpy, // dst.r src.r; copy: stack[dst] = stack[src]
    SlOp_lds, // dst.r src.r; load shared: stack[dst] = shared[src].value
    SlOp_sts, // dst.r src.r; store shared: shared[dst].value = stack[src]
    SlOp_mks, // dst.r src.r; make shared: stack[dst] = sharedSlot(src)
    SlOp_dts, // from.r to.r; detach shared: for i in from..=to { detach(stack[i]); }

    SlOp_add,  // dst.r lhs.r rhs.r; dst = lhs + rhs
    SlOp_sub,  // dst.r lhs.r rhs.r; dst = lhs - rhs
    SlOp_mul,  // dst.r lhs.r rhs.r; dst = lhs * rhs
    SlOp_div,  // dst.r lhs.r rhs.r; dst = lhs / rhs
    SlOp_mod,  // dst.r lhs.r rhs.r; dst = lhs % rhs
    SlOp_pow,  // dst.r lhs.r rhs.r; dst = lhs ^ rhs

    SlOp_print,// src.r; print(str(stack[src]) + '\n')
    SlOp_mkf,  // dst.r func.i; make function: stack[dst] = closure(funcs[func])
    SlOp_call, // func.r last.r;
               // stack[func] = stack[func](stack[func + 1], ..., stack[last])
    SlOp_ret,  // src.r

    SlOp_jmp, // diff.I; pc += diff; jump int
    SlOp_jtr, // val.r diff.I; jump if true int:   if (stack[val]) pc += diff
    SlOp_jfl, // val.r diff.I; jump if false int:   if (stack[val]) pc += diff
} SlOpCode;

SlObj slGenCode(SlVM *vm, SlSource *source);

#endif // !SL_CODEGEN_H_
