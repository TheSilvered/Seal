#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_vm.h"

/*

Instruction formats (32 bits):

All registers can be at most two bytes (hence the argument extensions)

 3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
|      r2       |      r1       |      rd       |     op      |x|  (A) Arithmetic
|      imm      |      r1       |      rd       |     op      |x|  (K) Immediate arithmetic
|      imm      |              rdx              |     op      |x|  (I) Immediate
|              r1x              |      rd       |     op      |x|  (C) Check op
|                      immx                     |     op      |0|  (J) Jump

 6 6 6 6 5 5 5 5 5 5 5 5 5 5 4 4 4 4 4 4 4 4 4 4 3 3 3 3 3 3 3 3
 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
|      r2u      |      r1u      |      rdu      |1 1 1 1 1 1 1 0|  (A) extension
|     immu      |      r1u      |      rdu      |1 1 1 1 1 1 1 0|  (K) extension
|                    immux                      |1 1 1 1 1 1 1 0|  (I) extension
|0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|      rdu      |1 1 1 1 1 1 1 0|  (C) extension

rd = destination register
r1 = first register
r2 = second register
imm = immediate value
x = use arg extension

-u = upper bits
-x = extended
*/

// R = register stack
// K = constants
// SH = shared values
// vals = [false, true]
typedef enum SlOpCode {
    // Binary ops

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

    SlOp_eq,      // (A) R[rd] = R[r1] == R[r2]
    SlOp_eqi,     // (K) R[rd] = R[r1] == int(imm)
    SlOp_ne,      // (A) R[rd] = R[r1] != R[r2]
    SlOp_nei,     // (K) R[rd] = R[r1] != int(imm)
    SlOp_lt,      // (A) R[rd] = R[r1] < R[r2]
    SlOp_lti,     // (K) R[rd] = R[r1] < int(imm)
    SlOp_le,      // (A) R[rd] = R[r1] <= R[r2]
    SlOp_lei,     // (K) R[rd] = R[r1] <= int(imm)
    SlOp_gt,      // (A) R[rd] = R[r1] > R[r2]
    SlOp_gti,     // (K) R[rd] = R[r1] > int(imm)
    SlOp_ge,      // (A) R[rd] = R[r1] >= R[r2]
    SlOp_gei,     // (K) R[rd] = R[r1] >= int(imm)

    // Register management

    SlOp_mov,     // (C) R[rd] = R[r1x]
    SlOp_ldn,     // (I) for (i = 0; i < uint(imm); i++) R[rdx + i] = null;
    SlOp_ldi,     // (I) R[rdx] = int(imm)
    SlOp_ldv,     // (I) R[rdx] = copy(vals[imm])
    SlOp_ldk,     // (I) R[rdx] = K[imm]

    // Shared slots

    SlOp_ldsh,    // (I) R[rdx] = SH[uint(imm)].value
    SlOp_stsh,    // (I) SH[uint(imm)].value = R[rdx]
    SlOp_mksh,    // (C) R[rd] = newShared(r1x)
    SlOp_dtsh,    // (I) for (i = 0; i < uint(imm); i++) R[rdx + i] = detach(S[rdx + i]);

    // Functions

    SlOp_mkf,     // (I) R[rdx] = newClosure(K[imm])
    SlOp_call,    // (I) R[rdx] = R[rdx](R[rdx + 1], ..., R[rdx + uint(imm)])
    SlOp_tcall,   // (I) R[rdx] = R[rdx](R[rdx + 1], ..., R[rdx + uint(imm)])
    SlOp_ret,     // (C) if (b) return R[r1x]; else return null;

    // Jump & tests

    SlOp_jmp,     // (J) pc += int(immx)

    SlOp_teq,     // (C) if (R[rd] == R[r1x]) pc++;
    SlOp_teqi,    // (I) if (R[rd] == int(imm)) pc++;
    SlOp_tne,     // (C) if (R[rd] != R[r1x]) pc++;
    SlOp_tnei,    // (I) if (R[rd] != int(imm)) pc++;
    SlOp_tlt,     // (C) if (R[rd] <  R[r1x]) pc++;
    SlOp_tlti,    // (I) if (R[rd] <  int(imm)) pc++;
    SlOp_tle,     // (C) if (R[rd] <= R[r1x]) pc++;
    SlOp_tlei,    // (I) if (R[rd] <= int(imm)) pc++;
    SlOp_tgt,     // (C) if (R[rd] >  R[r1x]) pc++;
    SlOp_tgti,    // (I) if (R[rd] >  int(imm)) pc++;
    SlOp_tge,     // (C) if (R[rd] >= R[r1x]) pc++;
    SlOp_tgei,    // (I) if (R[rd] >= int(imm)) pc++;
    SlOp_ttr,     // (I) if (truthy(R[r1x]) pc++;
    SlOp_tfl,     // (I) if (!truthy(R[r1x]) pc++;
    SlOp_tnl,     // (I) if (R[r1x] == null) pc++;
    SlOp_tnnl,    // (I) if (R[r1x] != null) pc++;

    // Collections

    // TODO: collection creation instructions

    SlOp_cget,    // (A) R[rd] = R[r1][R[r2]]
    SlOp_cgeti,   // (K) R[rd] = R[r1][int(r2)]
    SlOp_cgetk,   // (K) R[rd] = R[r1][K[r2]]
    SlOp_cset,    // (A) R[rd][R[r1]] = R[r2]
    SlOp_cseti,   // (A) R[rd][int(r1)] = R[r2]
    SlOp_csetk,   // (A) R[rd][K[r2]] = R[r2]

    // Other

    SlOp_print,   // (I) print(S[rdx]);
    SlOp_ext = 127
} SlOpCode;

SlObj slGenCode(SlVM *vm, const SlSource *source);

#endif // !SL_CODEGEN_H_
