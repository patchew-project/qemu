/*
 * RISC-V translation routines for the OFP8 Standard Extensions.
 *
 * Copyright (C) 2025 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define REQUIRE_ZVFOFP8MIN(ctx) do {        \
    if (!ctx->cfg_ptr->ext_zvfofp8min) {    \
        return false;                       \
    }                                       \
} while (0)


static bool trans_vfncvtbf16_sat_f_f_w(DisasContext *ctx, arg_rmr *a)
{
    REQUIRE_FPU;
    REQUIRE_ZVFOFP8MIN(ctx);

    if (opfv_narrow_check(ctx, a) && ctx->sew == MO_8) {
        gen_helper_gvec_3_ptr *fn;
        uint32_t data = 0;

        fn = ctx->altfmt ? gen_helper_vfncvtbf16_sat_f_f_w_ofp8e5m2 :
                           gen_helper_vfncvtbf16_sat_f_f_w_ofp8e4m3;

        gen_set_rm_chkfrm(ctx, RISCV_FRM_DYN);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, ctx->lmul);
        data = FIELD_DP32(data, VDATA, VTA, ctx->vta);
        data = FIELD_DP32(data, VDATA, VMA, ctx->vma);
        tcg_gen_gvec_3_ptr(vreg_ofs(ctx, a->rd), vreg_ofs(ctx, 0),
                           vreg_ofs(ctx, a->rs2), tcg_env,
                           ctx->cfg_ptr->vlenb,
                           ctx->cfg_ptr->vlenb, data, fn);
        finalize_rvv_inst(ctx);
        return true;
    }
    return false;
}
