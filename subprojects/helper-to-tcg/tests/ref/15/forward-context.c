#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "translate.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "forward-context.h"

#include "translate.h"
// i32 f0
void emit_f0(DisasContext *ctx, TCGv_i32 tmp2, TCGv_i32 a, TCGv_i32 off) {
TCGv_i32 tmp3 = tcg_temp_new_i32();
tmp3 = fdecl0(ctx, off);
tcg_gen_add_i32(tmp2, tmp3, a);
}

// i32 f1
void emit_f1(DisasContext *ctx, TCGv_i32 tmp2, TCGv_i32 a) {
TCGv_i32 tmp4 = tcg_temp_new_i32();
tmp4 = fdecl0(ctx, a);
tcg_gen_sub_i32(tmp2, tcg_constant_i32(0), a);
TCGv_i32 tmp3 = tcg_temp_new_i32();
tmp3 = fdecl0(ctx, tmp2);
tcg_gen_add_i32(tmp2, tmp3, tmp4);
}

