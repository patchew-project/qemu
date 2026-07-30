#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "vector.h"

// void helper_vec_splat_reg
void emit_vec_splat_reg(intptr_t d, TCGv_i32 imm) {
tcg_gen_gvec_dup_i32(MO_8, d, 32ull, 32ull, imm);
}

// void helper_vec_splat_imm
void emit_vec_splat_imm(intptr_t d, uint8_t imm) {
tcg_gen_gvec_dup_imm(MO_8, d, 32ull, 32ull, imm);
}

// void helper_vec_add
void emit_vec_add(intptr_t d, intptr_t a, intptr_t b) {
tcg_gen_gvec_add(MO_8, d, b, a, 32, 32);
}

// void helper_vec_add32
void emit_vec_add32(intptr_t d, intptr_t a, intptr_t b) {
tcg_gen_gvec_add(MO_32, d, b, a, 128, 128);
}

