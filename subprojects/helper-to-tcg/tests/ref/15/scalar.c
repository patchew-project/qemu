#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "scalar.h"

// i32 helper_add
void emit_add(TCGv_i32 tmp2, TCGv_i32 a, TCGv_i32 b) {
tcg_gen_add_i32(tmp2, b, a);
}

// i32 helper_cmov
void emit_cmov(TCGv_i32 tmp5, TCGv_i32 c0, TCGv_i32 c1, TCGv_i32 a, TCGv_i32 b) {
tcg_gen_movcond_i32(TCG_COND_LTU, tmp5, c0, c1, a, b);
}

