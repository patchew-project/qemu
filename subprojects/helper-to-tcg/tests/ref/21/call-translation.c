#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "call-translation.h"

// i32 inner0
void emit_inner0(TCGv_i32 tmp2, TCGv_i32 a) {
tcg_gen_add_i32(tmp2, a, tcg_constant_i32(5));
}

// i32 inner1
void emit_inner1(TCGv_i32 tmp2, TCGv_i32 a) {
tcg_gen_add_i32(tmp2, a, tcg_constant_i32(5));
}

// i32 outer1
void emit_outer1(TCGv_i32 tmp5, TCGv_i32 cond, TCGv_i32 b) {
TCGv_i32 tmp6 = tcg_temp_new_i32();
tcg_gen_add_i32(tmp6, b, tcg_constant_i32(5));
tcg_gen_movcond_i32(TCG_COND_EQ, tmp5, cond, tcg_constant_i32(0), tcg_constant_i32(0), tmp6);
}

