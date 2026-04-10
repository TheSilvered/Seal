#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_vm.h"

// Argument list:
// name.format
// Possible formats are:
// - r: register-like (1-byte for 0 to 127, two bytes above)
// - B: signed byte
// - b: unsigned byte
// - s: unsigned 16-bit integer (saved in big endian)
// - I: signed 24-bit integer (saved in big endian)
// - i: unsigned 24-bit integer (saved in big endian)

typedef enum SlOpCode {
    SlOp_nop, // no operation

    SlOp_ln,  // from.r to.r; load nulls: for i in from..=to { stack[i] = null; }
    SlOp_li8, // dst.r val.B; load int8_t: stack[dst] = int(val)
    SlOp_lkb, // dst.r src.b; load constant by byte:  stack[dst] = constants[src]
    SlOp_lks, // dst.r src.s; load constant by short: stack[dst] = constants[src]
    SlOp_lki, // dst.r src.i; load constant by short: stack[dst] = constants[src]
    SlOp_cpy, // dst.r src.r; copy: stack[dst] = stack[src]
    SlOp_ls,  // dst.r src.r; load shared: stack[dst] = shared[src].value
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
    SlOp_tcall,// func.r last.r; perform a tail call, args are the same as SlOp_call
    SlOp_ret,  // src.r; return src

    SlOp_jmp, // diff.I; jump: pc += diff
    SlOp_jtr, // val.r diff.I; jump if true: if (stack[val]) pc += diff
    SlOp_jfl, // val.r diff.I; jump if false: if (stack[val]) pc += diff
    SlOp_jlt, // lhs.r rhs.r diff.I; jump if lhs <  rhs: if (stack[lhs] <  stack[rhs]) pc += diff
    SlOp_jle, // lhs.r rhs.r diff.I; jump if lhs <= rhs: if (stack[lhs] <= stack[rhs]) pc += diff
    SlOp_jeq, // lhs.r rhs.r diff.I; jump if lhs == rhs: if (stack[lhs] == stack[rhs]) pc += diff
    SlOp_jne, // lhs.r rhs.r diff.I; jump if lhs != rhs: if (stack[lhs] != stack[rhs]) pc += diff
} SlOpCode;

SlObj slGenCode(SlVM *vm, const SlSource *source);

#endif // !SL_CODEGEN_H_
