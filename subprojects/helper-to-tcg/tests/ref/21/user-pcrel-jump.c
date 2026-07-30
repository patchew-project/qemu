#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "user-pcrel-jump.h"

// void prcel_jump
void emit_prcel_jump(TCGv_i32 off) {
TCGv_i32 ea = tcg_temp_new_i32();
tcg_gen_shl_i32(ea, off, tcg_constant_i32(2));
tcg_gen_or_i32(ea, ea, tcg_constant_i32(3));
target_pcrel_jump(ea);
}

// void prcel_condjump
void emit_prcel_condjump(TCGv_i32 cond, TCGv_i32 off) {
TCGLabel * label8 = gen_new_label();
TCGLabel * label9 = gen_new_label();
tcg_gen_brcondi_i32(TCG_COND_EQ, cond, 0, label8);
gen_set_label(label9);
TCGv_i32 ea = tcg_temp_new_i32();
tcg_gen_shl_i32(ea, off, tcg_constant_i32(2));
tcg_gen_or_i32(ea, ea, tcg_constant_i32(3));
target_pcrel_jump(ea);
tcg_gen_br(label8);
gen_set_label(label8);
}

