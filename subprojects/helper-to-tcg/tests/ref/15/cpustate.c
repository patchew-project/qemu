#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "cpustate.h"

extern TCGv_i32 tcg_regs[32];
extern TCGv_i32 tcg_a[8];
extern TCGv_i32 tcg_field;
// i32 helper_reg
void emit_reg(TCGv_i32 tmp5, TCGv_ptr env, uint32_t i) {
tcg_gen_mov_i32(tmp5, tcg_regs[((uint64_t) (uint32_t) i)]);
}

// i32 helper_data_a
void emit_data_a(TCGv_i32 tmp6, TCGv_ptr env, uint32_t i) {
tcg_gen_mov_i32(tmp6, tcg_a[((uint64_t) (uint32_t) i)]);
}

// i32 helper_single_mapped
void emit_single_mapped(TCGv_i32 tmp4, TCGv_ptr env) {
tcg_gen_mov_i32(tmp4, tcg_field);
}

// i32 helper_unmapped
void emit_unmapped(TCGv_i32 tmp2, TCGv_ptr env) {
TCGv_ptr ptr3 = tcg_temp_new_ptr();
tcg_gen_addi_ptr(ptr3, env, 128ull);
tcg_gen_ld_i32(tmp2, ptr3, 0);
}

