#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "call-to-declaration.h"

// i32 f0
void emit_f0(TCGv_i32 tmp2, TCGv_i32 tmp0, TCGv_i32 b) {
tmp2 = fdecl0(b);
}

// i32 f1
void emit_f1(TCGv_i32 tmp2, TCGv_i32 tmp0, uint32_t b) {
tmp2 = fdecl1(b);
}

// i32 f2
void emit_f2(TCGv_i32 tmp2, uint32_t b) {
tcg_gen_movi_i32(tmp2, imm1);
}

