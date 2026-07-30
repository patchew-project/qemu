#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "ldst.h"

// i32 helper_ld8
void emit_ld8(TCGv_i32 tmp4, TCGv_ptr env, TCGv_i32 addr) {
tcg_gen_qemu_ld_i32(tmp4, addr, tb_mmu_index(tcg_ctx->gen_tb->flags), MO_8);
}

// void helper_st8
void emit_st8(TCGv_ptr env, TCGv_i32 addr, TCGv_i32 data) {
tcg_gen_qemu_st_i32(data, addr, tb_mmu_index(tcg_ctx->gen_tb->flags), MO_8);
}

