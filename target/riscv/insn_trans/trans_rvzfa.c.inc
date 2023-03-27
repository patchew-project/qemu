/*
 * RISC-V translation routines for the Zfa Standard Extension.
 *
 * Copyright (c) 2023 Christoph Müllner, christoph.muellner@vrull.eu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_ZFA(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zfa) { \
        return false;             \
    }                             \
} while (0)

#define REQUIRE_ZFH(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zfh) { \
        return false;             \
    }                             \
} while (0)


static bool trans_fli_s(DisasContext *ctx, arg_fli_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    gen_helper_fli_s(dest, cpu_env, tcg_constant_i32(a->rs1));
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fli_d(DisasContext *ctx, arg_fli_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    gen_helper_fli_d(dest, cpu_env, tcg_constant_i32(a->rs1));
    gen_set_fpr_d(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fli_h(DisasContext *ctx, arg_fli_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    gen_helper_fli_h(dest, cpu_env, tcg_constant_i32(a->rs1));
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fminm_s(DisasContext *ctx, arg_fminm_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fminm_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmaxm_s(DisasContext *ctx, arg_fmaxm_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmaxm_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fminm_d(DisasContext *ctx, arg_fminm_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_d(ctx, a->rs2);

    gen_helper_fminm_d(dest, cpu_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmaxm_d(DisasContext *ctx, arg_fmaxm_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_d(ctx, a->rs2);

    gen_helper_fmaxm_d(dest, cpu_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fminm_h(DisasContext *ctx, arg_fminm_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fminm_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmaxm_h(DisasContext *ctx, arg_fmaxm_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmaxm_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fround_s(DisasContext *ctx, arg_fround_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fround_s(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_froundnx_s(DisasContext *ctx, arg_froundnx_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_froundnx_s(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fround_d(DisasContext *ctx, arg_fround_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fround_d(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_froundnx_d(DisasContext *ctx, arg_froundnx_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_froundnx_d(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fround_h(DisasContext *ctx, arg_fround_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fround_h(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_froundnx_h(DisasContext *ctx, arg_froundnx_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_froundnx_h(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fcvtmod_w_d(DisasContext *ctx, arg_fcvtmod_w_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);

    /* Rounding mode is RTZ. */
    gen_set_rm(ctx, RISCV_FRM_RTZ);
    gen_helper_fcvtmod_w_d(dest, cpu_env, src1);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

bool trans_fmvh_x_d(DisasContext *ctx, arg_fmvh_x_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);
    REQUIRE_32BIT(ctx);

    TCGv dst = dest_gpr(ctx, a->rd);
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_extract_i64(t1, cpu_fpr[a->rs1], 32, 32);
    tcg_gen_trunc_i64_tl(dst, t1);
    gen_set_gpr(ctx, a->rd, dst);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fmvp_d_x(DisasContext *ctx, arg_fmvp_d_x *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);
    REQUIRE_32BIT(ctx);

    TCGv src1 = get_gpr(ctx, a->rs1, EXT_ZERO);
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t1, src1);
    tcg_gen_deposit_i64(cpu_fpr[a->rd], cpu_fpr[a->rd], t1, 32, 32);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fleq_s(DisasContext *ctx, arg_fleq_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fleq_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fltq_s(DisasContext *ctx, arg_fltq_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fleq_d(DisasContext *ctx, arg_fleq_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fltq_d(DisasContext *ctx, arg_fltq_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

bool trans_fleq_h(DisasContext *ctx, arg_fleq_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fleq_h(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

bool trans_fltq_h(DisasContext *ctx, arg_fltq_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_h(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}
