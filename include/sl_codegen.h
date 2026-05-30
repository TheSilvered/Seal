#ifndef SL_CODEGEN_H_
#define SL_CODEGEN_H_

#include "sl_vm.h"

/*

Instruction formats (32 bits):

All registers can be at most two bytes (hence the argument extensions)

 3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0
 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
|      r2       |      r1       |      rd       |     op      |x|  (A)  Arithmetic
|      imm      |      r1       |      rd       |     op      |x|  (Ku) Immediate arithmetic (unsigned)
|      imm      |      r1       |      rd       |     op      |x|  (Ks) Immediate arithmetic (signed)
|      imm      |              rdx              |     op      |x|  (Iu) Immediate (unsigned)
|      imm      |              rdx              |     op      |x|  (Is) Immediate (signed)
|              r1x              |      rd       |     op      |x|  (T)  Two argument
|0 0 0 0 0 0 0 0|              rdx              |     op      |0|  (O)  One argument
|                      immx                     |     op      |0|  (J)  Jump

 6 6 6 6 5 5 5 5 5 5 5 5 5 5 4 4 4 4 4 4 4 4 4 4 3 3 3 3 3 3 3 3
 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
|      r2u      |      r1u      |      rdu      |1 1 1 1 1 1 1 0|  (A) extension
|     immu      |      r1u      |      rdu      |1 1 1 1 1 1 1 0|  (K) extension
|                    immux                      |1 1 1 1 1 1 1 0|  (I) extension
|0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0|      rdu      |1 1 1 1 1 1 1 0|  (D) extension

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
// vals = [false, true, null]
typedef enum SlOpCode {
    // Binary ops

    SlOp_add,     // (A)  R[rd] = R[r1] + R[r2]
    SlOp_addi,    // (Ks) R[rd] = R[r1] + imm
    SlOp_sub,     // (A)  R[rd] = R[r1] - R[r2]
    SlOp_subi,    // (Ks) R[rd] = R[r1] - imm
    SlOp_mul,     // (A)  R[rd] = R[r1] * R[r2]
    SlOp_muli,    // (Ks) R[rd] = R[r1] * imm
    SlOp_div,     // (A)  R[rd] = R[r1] / R[r2]
    SlOp_divi,    // (Ks) R[rd] = R[r1] / imm
    SlOp_mod,     // (A)  R[rd] = R[r1] % R[r2]
    SlOp_modi,    // (Ks) R[rd] = R[r1] % imm
    SlOp_pow,     // (A)  R[rd] = R[r1] ^ R[r2]
    SlOp_powi,    // (Ks) R[rd] = R[r1] ^ imm

    SlOp_eq,      // (A)  R[rd] = R[r1] == R[r2]
    SlOp_eqi,     // (Ks) R[rd] = R[r1] == imm
    SlOp_ne,      // (A)  R[rd] = R[r1] != R[r2]
    SlOp_nei,     // (Ks) R[rd] = R[r1] != imm
    SlOp_lt,      // (A)  R[rd] = R[r1] < R[r2]
    SlOp_lti,     // (Ks) R[rd] = R[r1] < imm
    SlOp_le,      // (A)  R[rd] = R[r1] <= R[r2]
    SlOp_lei,     // (Ks) R[rd] = R[r1] <= imm
    SlOp_gt,      // (A)  R[rd] = R[r1] > R[r2]
    SlOp_gti,     // (Ks) R[rd] = R[r1] > imm
    SlOp_ge,      // (A)  R[rd] = R[r1] >= R[r2]
    SlOp_gei,     // (Ks) R[rd] = R[r1] >= imm

    // Register management

    SlOp_mov,     // (T)  R[rd] = R[r1x]
    SlOp_ldn,     // (Iu) for (i = 0; i < imm; i++) R[rdx + i] = null
    SlOp_ldi,     // (Is) R[rdx] = imm
    SlOp_ldv,     // (Iu) R[rdx] = copy(vals[imm])
    SlOp_ldk,     // (Iu) R[rdx] = K[imm]

    // Shared slots

    SlOp_ldsh,    // (Iu) R[rdx] = SH[imm].value
    SlOp_stsh,    // (Iu) SH[imm].value = R[rdx]
    SlOp_mksh,    // (T)  R[rd] = newShared(r1x)
    SlOp_dtsh,    // (Iu) for (i = 0; i < imm; i++) R[rdx + i] = detach(S[rdx + i])

    // Functions

    SlOp_mkf,     // (Iu)  R[rdx] = newClosure(K[imm])
    SlOp_call,    // (Iu)  R[rdx] = R[rdx](R[rdx + 1], ..., R[rdx + imm])
    SlOp_tcall,   // (Iu)  R[rdx] = R[rdx](R[rdx + 1], ..., R[rdx + imm])
    SlOp_ret,     // (O)   return R[rdx]
    SlOp_retv,    // (Iu)  return vals[imm]

    // Jump & tests

    SlOp_jmp,     // (J)  pc += immx  ; offset relative to the next instruction

    SlOp_teq,     // (T)  if (R[rd]  == R[r1x]) pc++
    SlOp_teqi,    // (Is) if (R[rdx] == imm)    pc++
    SlOp_tne,     // (T)  if (R[rd]  != R[r1x]) pc++
    SlOp_tnei,    // (Is) if (R[rdx] != imm)    pc++
    SlOp_tlt,     // (T)  if (R[rd]  <  R[r1x]) pc++
    SlOp_tlti,    // (Is) if (R[rdx] <  imm)    pc++
    SlOp_tle,     // (T)  if (R[rd]  <= R[r1x]) pc++
    SlOp_tlei,    // (Is) if (R[rdx] <= imm)    pc++
    SlOp_tgt,     // (T)  if (R[rd]  >  R[r1x]) pc++
    SlOp_tgti,    // (Is) if (R[rdx] >  imm)    pc++
    SlOp_tge,     // (T)  if (R[rd]  >= R[r1x]) pc++
    SlOp_tgei,    // (Is) if (R[rdx] >= imm)    pc++
    SlOp_ttr,     // (O)  if (truthy(R[rdx])    pc++
    SlOp_tfl,     // (O)  if (!truthy(R[rdx])   pc++
    SlOp_tnl,     // (O)  if (R[rdx] == null)   pc++
    SlOp_tnnl,    // (O)  if (R[rdx] != null)   pc++

    // Collections

    // TODO: collection creation instructions

    SlOp_cget,    // (A)  R[rd] = R[r1][R[r2]]
    SlOp_cgeti,   // (Ks) R[rd] = R[r1][imm]
    SlOp_cgetk,   // (Ku) R[rd] = R[r1][K[imm]]
    SlOp_cset,    // (A)  R[rd][R[r1]] = R[r2]
    SlOp_cseti,   // (Ks)  R[rd][imm] = R[r1]
    SlOp_csetk,   // (Ku)  R[rd][K[imm]] = R[r2]

    // Other

    SlOp_print,   // (O) print(S[rdx]) ; placeholder
    SlOp_ext = 127
} SlOpCode;

SlObj slGenCode(SlVM *vm, const SlSource *source);

#endif // !SL_CODEGEN_H_
