#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_vm.h"

/*

Instruction formats (32 bits):

All registers can be at most two bytes (hence the argument extensions)

 3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
|0|      r2       |      r1       |      rd       |    op     |x|  (A) Arithmetic
|       imm       |      r1       |      rd       |    op     |x|  (K) Immediate arithmetic
|       imm       |              rdx              |    op     |x|  (I) Immediate
|b|              r1x              |      rd       |    op     |x|  (C) Check op
|                       immx                      |    op     |0|  (J) Jump

 6 6 6 6 5 5 5 5 5 5 5 5 5 5 4 4 4 4 4 4 4 4 4 4 3 3 3 3 3 3 3 3
 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
|0|      r2u      |      r1u      |      rdu      |1 1 1 1 1 1 0|  (A) extension
|      immu       |      r1u      |      rdu      |1 1 1 1 1 1 0|  (K) extension
|0 0|                    immux                    |1 1 1 1 1 1 0|  (I) extension
|0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|      rdu      |1 1 1 1 1 1 0|  (C) extension

rd = destination register
r1 = first register
r2 = second register
imm = immediate value
x = use arg extension
b = boolean (0=false, 1=true)

-u = upper bits
-x = extended
*/

// R = register stack
// K = constants
// SH = shared values
// vals = [false, true]
typedef enum SlOpCode {
    SlOp_add = 2, // (A) R[rd] = R[r1] + R[r2]
    SlOp_addi,    // (K) R[rd] = R[r1] + int(imm)
    SlOp_sub,     // (A) R[rd] = R[r1] - R[r2]
    SlOp_subi,    // (K) R[rd] = R[r1] - int(imm)
    SlOp_mul,     // (A) R[rd] = R[r1] * R[r2]
    SlOp_muli,    // (K) R[rd] = R[r1] * int(imm)
    SlOp_div,     // (A) R[rd] = R[r1] / R[r2]
    SlOp_divi,    // (K) R[rd] = R[r1] / int(imm)
    SlOp_mod,     // (A) R[rd] = R[r1] % R[r2]
    SlOp_modi,    // (K) R[rd] = R[r1] % int(imm)
    SlOp_pow,     // (A) R[rd] = R[r1] ^ R[r2]
    SlOp_powi,    // (K) R[rd] = R[r1] ^ int(imm)

    SlOp_mov,     // (C) R[rd] = R[r1x]
    SlOp_ldn,     // (I) for (i = 0; i < uint(imm); i++) R[rdx + i] = null;
    SlOp_ldi,     // (I) R[rdx] = int(imm)
    SlOp_ldv,     // (I) R[rdx] = copy(vals[imm])
    SlOp_ldk,     // (I) R[rdx] = K[imm]

    SlOp_ldsh,    // (I) R[rdx] = SH[uint(imm)].value
    SlOp_stsh,    // (I) SH[uint(imm)].value = R[rdx]
    SlOp_mksh,    // (C) R[rd] = newShared(r1x)
    SlOp_dtsh,    // (I) for (i = 0; i < uint(imm); i++) R[rdx + i] = detach(S[rdx + i]);

    SlOp_mkf,     // (I) R[rdx] = newClosure(K[imm])
    SlOp_call,    // (I) R[rdx] = R[rdx](R[rdx + 1], ..., R[rdx + uint(imm)])
    SlOp_tcall,   // (I) R[rdx] = R[rdx](R[rdx + 1], ..., R[rdx + uint(imm)])
    SlOp_ret,     // (C) if (b) return R[r1x]; else return null;

    SlOp_jmp,     // (J) pc += int(immx)

    SlOp_eqj,     // (C) if ((R[rd] == R[r1x]) == b) pc++;
    SlOp_eqi,     // (C) if ((R[rd] == int(r1x) == b) pc++;
    SlOp_ltj,     // (C) if ((R[rd] < R[r1x]) == b) pc++;
    SlOp_lej,     // (C) if ((R[rd] <= R[r1x]) == b) pc++;
    SlOp_boolj,   // (C) if (R[rd] == b) pc++;

    SlOp_print,   // (C) print(S[r1x]);
} SlOpCode;

SlObj slGenCode(SlVM *vm, const SlSource *source);

#endif // !SL_CODEGEN_H_
