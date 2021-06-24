/*
 * RISC-V translation routines for the RVP Standard Extension.
 *
 * Copyright (c) 2021 T-Head Semiconductor Co., Ltd. All rights reserved.
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

#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "tcg/tcg.h"

/*
 *** SIMD Data Processing Instructions
 */

/* 16-bit Addition & Subtraction Instructions */

/*
 * For some instructions, such as add16, an oberservation can be utilized:
 * 1) If any reg is zero, it can be reduced to an inline op on the whole reg.
 * 2) Otherwise, it can be acclebrated by an vec op.
 */
static inline bool
r_inline(DisasContext *ctx, arg_r *a,
         void (* vecop)(TCGv, TCGv, TCGv),
         void (* op)(TCGv, TCGv, TCGv))
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }
    if (a->rd && a->rs1 && a->rs2) {
        vecop(cpu_gpr[a->rd], cpu_gpr[a->rs1], cpu_gpr[a->rs2]);
    } else {
        gen_arith(ctx, a, op);
    }
    return true;
}

/* Complete inline implementation */
#define GEN_RVP_R_INLINE(NAME, VECOP, OP)                \
static bool trans_##NAME(DisasContext *s, arg_r *a)      \
{                                                        \
    return r_inline(s, a, VECOP, OP);                    \
}

GEN_RVP_R_INLINE(add16, tcg_gen_vec_add16_tl, tcg_gen_add_tl);
GEN_RVP_R_INLINE(sub16, tcg_gen_vec_sub16_tl, tcg_gen_sub_tl);

/* Out of line helpers for R format packed instructions */
static inline bool
r_ool(DisasContext *ctx, arg_r *a, void (* fn)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    TCGv src1, src2, dst;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    dst = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    fn(dst, cpu_env, src1, src2);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free(dst);
    return true;
}

#define GEN_RVP_R_OOL(NAME)                            \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_ool(s, a, gen_helper_##NAME);             \
}

GEN_RVP_R_OOL(radd16);
GEN_RVP_R_OOL(uradd16);
GEN_RVP_R_OOL(kadd16);
GEN_RVP_R_OOL(ukadd16);
GEN_RVP_R_OOL(rsub16);
GEN_RVP_R_OOL(ursub16);
GEN_RVP_R_OOL(ksub16);
GEN_RVP_R_OOL(uksub16);
GEN_RVP_R_OOL(cras16);
GEN_RVP_R_OOL(rcras16);
GEN_RVP_R_OOL(urcras16);
GEN_RVP_R_OOL(kcras16);
GEN_RVP_R_OOL(ukcras16);
GEN_RVP_R_OOL(crsa16);
GEN_RVP_R_OOL(rcrsa16);
GEN_RVP_R_OOL(urcrsa16);
GEN_RVP_R_OOL(kcrsa16);
GEN_RVP_R_OOL(ukcrsa16);
GEN_RVP_R_OOL(stas16);
GEN_RVP_R_OOL(rstas16);
GEN_RVP_R_OOL(urstas16);
GEN_RVP_R_OOL(kstas16);
GEN_RVP_R_OOL(ukstas16);
GEN_RVP_R_OOL(stsa16);
GEN_RVP_R_OOL(rstsa16);
GEN_RVP_R_OOL(urstsa16);
GEN_RVP_R_OOL(kstsa16);
GEN_RVP_R_OOL(ukstsa16);

/* 8-bit Addition & Subtraction Instructions */
GEN_RVP_R_INLINE(add8, tcg_gen_vec_add8_tl, tcg_gen_add_tl);
GEN_RVP_R_INLINE(sub8, tcg_gen_vec_sub8_tl, tcg_gen_sub_tl);

GEN_RVP_R_OOL(radd8);
GEN_RVP_R_OOL(uradd8);
GEN_RVP_R_OOL(kadd8);
GEN_RVP_R_OOL(ukadd8);
GEN_RVP_R_OOL(rsub8);
GEN_RVP_R_OOL(ursub8);
GEN_RVP_R_OOL(ksub8);
GEN_RVP_R_OOL(uksub8);

/* 16-bit Shift Instructions */
GEN_RVP_R_OOL(sra16);
GEN_RVP_R_OOL(srl16);
GEN_RVP_R_OOL(sll16);
GEN_RVP_R_OOL(sra16_u);
GEN_RVP_R_OOL(srl16_u);
GEN_RVP_R_OOL(ksll16);
GEN_RVP_R_OOL(kslra16);
GEN_RVP_R_OOL(kslra16_u);

static bool
rvp_shifti_ool(DisasContext *ctx, arg_shift *a,
               void (* fn)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    TCGv src1, dst, shift;

    src1 = tcg_temp_new();
    dst = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    shift = tcg_const_tl(a->shamt);
    fn(dst, cpu_env, src1, shift);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(dst);
    tcg_temp_free(shift);
    return true;
}

static inline bool
rvp_shifti(DisasContext *ctx, arg_shift *a,
           void (* vecop)(TCGv, TCGv, target_long),
           void (* op)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    if (a->rd && a->rs1 && vecop) {
        vecop(cpu_gpr[a->rd], cpu_gpr[a->rs1], a->shamt);
        return true;
    }
    return rvp_shifti_ool(ctx, a, op);
}

#define GEN_RVP_SHIFTI(NAME, VECOP, OP)                  \
static bool trans_##NAME(DisasContext *s, arg_shift *a)  \
{                                                        \
    return rvp_shifti(s, a, VECOP, OP);                  \
}

GEN_RVP_SHIFTI(srai16, tcg_gen_vec_sar16i_tl, gen_helper_sra16);
GEN_RVP_SHIFTI(srli16, tcg_gen_vec_shr16i_tl, gen_helper_srl16);
GEN_RVP_SHIFTI(slli16, tcg_gen_vec_shl16i_tl, gen_helper_sll16);
GEN_RVP_SHIFTI(srai16_u, NULL, gen_helper_sra16_u);
GEN_RVP_SHIFTI(srli16_u, NULL, gen_helper_srl16_u);
GEN_RVP_SHIFTI(kslli16, NULL, gen_helper_ksll16);

/* SIMD 8-bit Shift Instructions */
GEN_RVP_R_OOL(sra8);
GEN_RVP_R_OOL(srl8);
GEN_RVP_R_OOL(sll8);
GEN_RVP_R_OOL(sra8_u);
GEN_RVP_R_OOL(srl8_u);
GEN_RVP_R_OOL(ksll8);
GEN_RVP_R_OOL(kslra8);
GEN_RVP_R_OOL(kslra8_u);
GEN_RVP_SHIFTI(srai8, tcg_gen_vec_sar8i_tl, gen_helper_sra8);
GEN_RVP_SHIFTI(srli8, tcg_gen_vec_shr8i_tl, gen_helper_srl8);
GEN_RVP_SHIFTI(slli8, tcg_gen_vec_shl8i_tl, gen_helper_sll8);
GEN_RVP_SHIFTI(srai8_u, NULL, gen_helper_sra8_u);
GEN_RVP_SHIFTI(srli8_u, NULL, gen_helper_srl8_u);
GEN_RVP_SHIFTI(kslli8, NULL, gen_helper_ksll8);

/* SIMD 16-bit Compare Instructions */
GEN_RVP_R_OOL(cmpeq16);
GEN_RVP_R_OOL(scmplt16);
GEN_RVP_R_OOL(scmple16);
GEN_RVP_R_OOL(ucmplt16);
GEN_RVP_R_OOL(ucmple16);

/* SIMD 8-bit Compare Instructions */
GEN_RVP_R_OOL(cmpeq8);
GEN_RVP_R_OOL(scmplt8);
GEN_RVP_R_OOL(scmple8);
GEN_RVP_R_OOL(ucmplt8);
GEN_RVP_R_OOL(ucmple8);

/* SIMD 16-bit Multiply Instructions */
static void set_pair_regs(DisasContext *ctx, TCGv_i64 dst, int rd)
{
    TCGv t1, t2;

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();

    if (is_32bit(ctx)) {
        TCGv_i32 lo, hi;

        lo = tcg_temp_new_i32();
        hi = tcg_temp_new_i32();
        tcg_gen_extr_i64_i32(lo, hi, dst);

        tcg_gen_ext_i32_tl(t1, lo);
        tcg_gen_ext_i32_tl(t2, hi);

        gen_set_gpr(rd, t1);
        gen_set_gpr(rd + 1, t2);
        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    } else {
        tcg_gen_trunc_i64_tl(t1, dst);
        gen_set_gpr(rd, t1);
    }
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static inline bool
r_d64_ool(DisasContext *ctx, arg_r *a,
          void (* fn)(TCGv_i64, TCGv_ptr, TCGv, TCGv))
{
    TCGv t1, t2;
    TCGv_i64 t3;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    t3 = tcg_temp_new_i64();

    gen_get_gpr(t1, a->rs1);
    gen_get_gpr(t2, a->rs2);
    fn(t3, cpu_env, t1, t2);
    set_pair_regs(ctx, t3, a->rd);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_temp_free_i64(t3);
    return true;
}

#define GEN_RVP_R_D64_OOL(NAME)                        \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_d64_ool(s, a, gen_helper_##NAME);         \
}

GEN_RVP_R_D64_OOL(smul16);
GEN_RVP_R_D64_OOL(smulx16);
GEN_RVP_R_D64_OOL(umul16);
GEN_RVP_R_D64_OOL(umulx16);
GEN_RVP_R_OOL(khm16);
GEN_RVP_R_OOL(khmx16);

/* SIMD 8-bit Multiply Instructions */
GEN_RVP_R_D64_OOL(smul8);
GEN_RVP_R_D64_OOL(smulx8);
GEN_RVP_R_D64_OOL(umul8);
GEN_RVP_R_D64_OOL(umulx8);
GEN_RVP_R_OOL(khm8);
GEN_RVP_R_OOL(khmx8);

/* SIMD 16-bit Miscellaneous Instructions */
GEN_RVP_R_OOL(smin16);
GEN_RVP_R_OOL(umin16);
GEN_RVP_R_OOL(smax16);
GEN_RVP_R_OOL(umax16);
GEN_RVP_SHIFTI(sclip16, NULL, gen_helper_sclip16);
GEN_RVP_SHIFTI(uclip16, NULL, gen_helper_uclip16);

/* Out of line helpers for R2 format */
static bool
r2_ool(DisasContext *ctx, arg_r2 *a,
       void (* fn)(TCGv, TCGv_ptr, TCGv))
{
    TCGv src1, dst;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new();
    dst = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    fn(dst, cpu_env, src1);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(dst);
    return true;
}

#define GEN_RVP_R2_OOL(NAME)                           \
static bool trans_##NAME(DisasContext *s, arg_r2 *a)   \
{                                                      \
    return r2_ool(s, a, gen_helper_##NAME);            \
}

GEN_RVP_R2_OOL(kabs16);
GEN_RVP_R2_OOL(clrs16);
GEN_RVP_R2_OOL(clz16);
GEN_RVP_R2_OOL(clo16);

/* SIMD 8-bit Miscellaneous Instructions */
GEN_RVP_R_OOL(smin8);
GEN_RVP_R_OOL(umin8);
GEN_RVP_R_OOL(smax8);
GEN_RVP_R_OOL(umax8);
GEN_RVP_SHIFTI(sclip8, NULL, gen_helper_sclip8);
GEN_RVP_SHIFTI(uclip8, NULL, gen_helper_uclip8);
GEN_RVP_R2_OOL(kabs8);
GEN_RVP_R2_OOL(clrs8);
GEN_RVP_R2_OOL(clz8);
GEN_RVP_R2_OOL(clo8);
GEN_RVP_R2_OOL(swap8);

/* 8-bit Unpacking Instructions */
GEN_RVP_R2_OOL(sunpkd810);
GEN_RVP_R2_OOL(sunpkd820);
GEN_RVP_R2_OOL(sunpkd830);
GEN_RVP_R2_OOL(sunpkd831);
GEN_RVP_R2_OOL(sunpkd832);
GEN_RVP_R2_OOL(zunpkd810);
GEN_RVP_R2_OOL(zunpkd820);
GEN_RVP_R2_OOL(zunpkd830);
GEN_RVP_R2_OOL(zunpkd831);
GEN_RVP_R2_OOL(zunpkd832);

/*
 *** Partial-SIMD Data Processing Instruction
 */
/* 16-bit Packing Instructions */
GEN_RVP_R_OOL(pkbb16);
GEN_RVP_R_OOL(pkbt16);
GEN_RVP_R_OOL(pktt16);
GEN_RVP_R_OOL(pktb16);

/* Most Significant Word “32x32” Multiply & Add Instructions */
GEN_RVP_R_OOL(smmul);
GEN_RVP_R_OOL(smmul_u);

/* Function to accumulate destination register */
static inline bool r_acc_ool(DisasContext *ctx, arg_r *a,
                             void (* fn)(TCGv, TCGv_ptr, TCGv, TCGv, TCGv))
{
    TCGv src1, src2, src3, dst;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    src3 = tcg_temp_new();
    dst = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    gen_get_gpr(src3, a->rd);
    fn(dst, cpu_env, src1, src2, src3);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free(src3);
    tcg_temp_free(dst);
    return true;
}

#define GEN_RVP_R_ACC_OOL(NAME)                        \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_acc_ool(s, a, gen_helper_##NAME);         \
}

GEN_RVP_R_ACC_OOL(kmmac);
GEN_RVP_R_ACC_OOL(kmmac_u);
GEN_RVP_R_ACC_OOL(kmmsb);
GEN_RVP_R_ACC_OOL(kmmsb_u);
GEN_RVP_R_OOL(kwmmul);
GEN_RVP_R_OOL(kwmmul_u);

/* Most Significant Word “32x16” Multiply & Add Instructions */
GEN_RVP_R_OOL(smmwb);
GEN_RVP_R_OOL(smmwb_u);
GEN_RVP_R_OOL(smmwt);
GEN_RVP_R_OOL(smmwt_u);
GEN_RVP_R_ACC_OOL(kmmawb);
GEN_RVP_R_ACC_OOL(kmmawb_u);
GEN_RVP_R_ACC_OOL(kmmawt);
GEN_RVP_R_ACC_OOL(kmmawt_u);
GEN_RVP_R_OOL(kmmwb2);
GEN_RVP_R_OOL(kmmwb2_u);
GEN_RVP_R_OOL(kmmwt2);
GEN_RVP_R_OOL(kmmwt2_u);
GEN_RVP_R_ACC_OOL(kmmawb2);
GEN_RVP_R_ACC_OOL(kmmawb2_u);
GEN_RVP_R_ACC_OOL(kmmawt2);
GEN_RVP_R_ACC_OOL(kmmawt2_u);

/* Signed 16-bit Multiply with 32-bit Add/Subtract Instructions */
GEN_RVP_R_OOL(smbb16);
GEN_RVP_R_OOL(smbt16);
GEN_RVP_R_OOL(smtt16);
GEN_RVP_R_OOL(kmda);
GEN_RVP_R_OOL(kmxda);
GEN_RVP_R_OOL(smds);
GEN_RVP_R_OOL(smdrs);
GEN_RVP_R_OOL(smxds);
GEN_RVP_R_ACC_OOL(kmabb);
GEN_RVP_R_ACC_OOL(kmabt);
GEN_RVP_R_ACC_OOL(kmatt);
GEN_RVP_R_ACC_OOL(kmada);
GEN_RVP_R_ACC_OOL(kmaxda);
GEN_RVP_R_ACC_OOL(kmads);
GEN_RVP_R_ACC_OOL(kmadrs);
GEN_RVP_R_ACC_OOL(kmaxds);
GEN_RVP_R_ACC_OOL(kmsda);
GEN_RVP_R_ACC_OOL(kmsxda);

/* Signed 16-bit Multiply with 64-bit Add/Subtract Instructions */
static bool
r_d64_s64_ool(DisasContext *ctx, arg_r *a,
              void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv src2;
    TCGv_i64 src1, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new();
    dst = tcg_temp_new_i64();

    if (is_32bit(ctx)) {
        TCGv t0, t1;
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        gen_get_gpr(t1, a->rs1 + 1);
        tcg_gen_concat_tl_i64(src1, t0, t1);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
    } else {
        TCGv t0;
        t0 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        tcg_gen_ext_tl_i64(src1, t0);
        tcg_temp_free(t0);
    }

    gen_get_gpr(src2, a->rs2);
    fn(dst, cpu_env, src1, src2);
    set_pair_regs(ctx, dst, a->rd);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i64(dst);
    tcg_temp_free(src2);
    return true;
}

#define GEN_RVP_R_D64_S64_OOL(NAME)                    \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_d64_s64_ool(s, a, gen_helper_##NAME);     \
}

GEN_RVP_R_D64_S64_OOL(smal);

/* Partial-SIMD Miscellaneous Instructions */
GEN_RVP_SHIFTI(sclip32, NULL, gen_helper_sclip32);
GEN_RVP_SHIFTI(uclip32, NULL, gen_helper_uclip32);
GEN_RVP_R2_OOL(clrs32);
GEN_RVP_R2_OOL(clz32);
GEN_RVP_R2_OOL(clo32);
GEN_RVP_R_OOL(pbsad);
GEN_RVP_R_ACC_OOL(pbsada);

/* 8-bit Multiply with 32-bit Add Instructions */
GEN_RVP_R_ACC_OOL(smaqa);
GEN_RVP_R_ACC_OOL(umaqa);
GEN_RVP_R_ACC_OOL(smaqa_su);

/*
 *** 64-bit Profile Instructions
 */
/* 64-bit Addition & Subtraction Instructions */
static bool
r_d64_s64_s64_ool(DisasContext *ctx, arg_r *a,
                  void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv t1, t2;
    TCGv_i64 src1, src2, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();

    if (is_32bit(ctx)) {
        TCGv a0, a1, b0, b1;
        a0 = tcg_temp_new();
        a1 = tcg_temp_new();
        b0 = tcg_temp_new();
        b1 = tcg_temp_new();

        gen_get_gpr(a0, a->rs1);
        gen_get_gpr(a1, a->rs1 + 1);
        tcg_gen_concat_tl_i64(src1, a0, a1);
        gen_get_gpr(b0, a->rs2);
        gen_get_gpr(b1, a->rs2 + 1);
        tcg_gen_concat_tl_i64(src2, b0, b1);

        tcg_temp_free(a0);
        tcg_temp_free(a1);
        tcg_temp_free(b0);
        tcg_temp_free(b1);
    } else {
        t1 = tcg_temp_new();
        t2 = tcg_temp_new();
        gen_get_gpr(t1, a->rs1);
        tcg_gen_ext_tl_i64(src1, t1);
        gen_get_gpr(t2, a->rs2);
        tcg_gen_ext_tl_i64(src2, t2);
        tcg_temp_free(t1);
        tcg_temp_free(t2);
    }

    fn(dst, cpu_env, src1, src2);
    set_pair_regs(ctx, dst, a->rd);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i64(src2);
    tcg_temp_free_i64(dst);
    return true;
}

#define GEN_RVP_R_D64_S64_S64_OOL(NAME)                   \
static bool trans_##NAME(DisasContext *s, arg_r *a)       \
{                                                         \
    return r_d64_s64_s64_ool(s, a, gen_helper_##NAME);    \
}

GEN_RVP_R_D64_S64_S64_OOL(add64);
GEN_RVP_R_D64_S64_S64_OOL(radd64);
GEN_RVP_R_D64_S64_S64_OOL(uradd64);
GEN_RVP_R_D64_S64_S64_OOL(kadd64);
GEN_RVP_R_D64_S64_S64_OOL(ukadd64);
GEN_RVP_R_D64_S64_S64_OOL(sub64);
GEN_RVP_R_D64_S64_S64_OOL(rsub64);
GEN_RVP_R_D64_S64_S64_OOL(ursub64);
GEN_RVP_R_D64_S64_S64_OOL(ksub64);
GEN_RVP_R_D64_S64_S64_OOL(uksub64);

/* 32-bit Multiply with 64-bit Add/Subtract Instructions */

/* Function to accumulate 64bit destination register */
static bool
r_d64_acc_ool(DisasContext *ctx, arg_r *a,
              void (* fn)(TCGv_i64, TCGv_ptr, TCGv, TCGv, TCGv_i64))
{
    TCGv src1, src2;
    TCGv_i64 dst, src3;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    src3 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);

    if (is_32bit(ctx)) {
        TCGv t0, t1;
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();

        gen_get_gpr(t0, a->rd);
        gen_get_gpr(t1, a->rd + 1);
        tcg_gen_concat_tl_i64(src3, t0, t1);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
    } else {
        TCGv t0;
        t0 = tcg_temp_new();

        gen_get_gpr(t0, a->rd);
        tcg_gen_ext_tl_i64(src3, t0);
        tcg_temp_free(t0);
    }

    fn(dst, cpu_env, src1, src2, src3);

    set_pair_regs(ctx, dst, a->rd);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free_i64(src3);
    tcg_temp_free_i64(dst);
    return true;
}

#define GEN_RVP_R_D64_ACC_OOL(NAME)                    \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_d64_acc_ool(s, a, gen_helper_##NAME);     \
}

GEN_RVP_R_D64_ACC_OOL(smar64);
GEN_RVP_R_D64_ACC_OOL(smsr64);
GEN_RVP_R_D64_ACC_OOL(umar64);
GEN_RVP_R_D64_ACC_OOL(umsr64);
GEN_RVP_R_D64_ACC_OOL(kmar64);
GEN_RVP_R_D64_ACC_OOL(kmsr64);
GEN_RVP_R_D64_ACC_OOL(ukmar64);
GEN_RVP_R_D64_ACC_OOL(ukmsr64);

/* Signed 16-bit Multiply with 64-bit Add/Subtract Instructions */
GEN_RVP_R_D64_ACC_OOL(smalbb);
GEN_RVP_R_D64_ACC_OOL(smalbt);
GEN_RVP_R_D64_ACC_OOL(smaltt);
GEN_RVP_R_D64_ACC_OOL(smalda);
GEN_RVP_R_D64_ACC_OOL(smalxda);
GEN_RVP_R_D64_ACC_OOL(smalds);
GEN_RVP_R_D64_ACC_OOL(smaldrs);
GEN_RVP_R_D64_ACC_OOL(smalxds);
GEN_RVP_R_D64_ACC_OOL(smslda);
GEN_RVP_R_D64_ACC_OOL(smslxda);

/*
 *** Non-SIMD Instructions
 */
/* Non-SIMD Q15 saturation ALU Instructions */
GEN_RVP_R_OOL(kaddh);
GEN_RVP_R_OOL(ksubh);
GEN_RVP_R_OOL(khmbb);
GEN_RVP_R_OOL(khmbt);
GEN_RVP_R_OOL(khmtt);
GEN_RVP_R_OOL(ukaddh);
GEN_RVP_R_OOL(uksubh);

/* Non-SIMD Q31 saturation ALU Instructions */
GEN_RVP_R_OOL(kaddw);
GEN_RVP_R_OOL(ukaddw);
GEN_RVP_R_OOL(ksubw);
GEN_RVP_R_OOL(uksubw);
GEN_RVP_R_OOL(kdmbb);
GEN_RVP_R_OOL(kdmbt);
GEN_RVP_R_OOL(kdmtt);
GEN_RVP_R_OOL(kslraw);
GEN_RVP_R_OOL(kslraw_u);
GEN_RVP_R_OOL(ksllw);
GEN_RVP_SHIFTI(kslliw, NULL, gen_helper_ksllw);
GEN_RVP_R_ACC_OOL(kdmabb);
GEN_RVP_R_ACC_OOL(kdmabt);
GEN_RVP_R_ACC_OOL(kdmatt);
GEN_RVP_R2_OOL(kabsw);

/* 32-bit Computation Instructions */
GEN_RVP_R_OOL(raddw);
GEN_RVP_R_OOL(uraddw);
GEN_RVP_R_OOL(rsubw);
GEN_RVP_R_OOL(ursubw);
GEN_RVP_R_OOL(minw);
GEN_RVP_R_OOL(maxw);
GEN_RVP_R_D64_OOL(mulr64);
GEN_RVP_R_D64_OOL(mulsr64);

/* Non-SIMD Miscellaneous Instructions */
GEN_RVP_R_OOL(ave);
GEN_RVP_R_OOL(sra_u);
GEN_RVP_SHIFTI(srai_u, NULL, gen_helper_sra_u);
GEN_RVP_R_OOL(bitrev);
GEN_RVP_SHIFTI(bitrevi, NULL, gen_helper_bitrev);

static bool
r_s64_ool(DisasContext *ctx, arg_r *a,
          void (* fn)(TCGv, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 src1;
    TCGv src2, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new();
    dst = tcg_temp_new();

    if (is_32bit(ctx)) {
        TCGv t0, t1;
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        gen_get_gpr(t1, a->rs1 + 1);
        tcg_gen_concat_tl_i64(src1, t0, t1);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
    } else {
        TCGv t0;
        t0 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        tcg_gen_ext_tl_i64(src1, t0);
        tcg_temp_free(t0);
    }
    gen_get_gpr(src2, a->rs2);
    fn(dst, cpu_env, src1, src2);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free_i64(src1);
    tcg_temp_free(src2);
    tcg_temp_free(dst);
    return true;
}

#define GEN_RVP_R_S64_OOL(NAME)                        \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_s64_ool(s, a, gen_helper_##NAME);         \
}

GEN_RVP_R_S64_OOL(wext);

static bool rvp_shifti_s64_ool(DisasContext *ctx, arg_shift *a,
                               void (* fn)(TCGv, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 src1;
    TCGv shift, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_psfoperand) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    dst = tcg_temp_new();

    if (is_32bit(ctx)) {
        TCGv t0, t1;
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        gen_get_gpr(t1, a->rs1 + 1);
        tcg_gen_concat_tl_i64(src1, t0, t1);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
    } else {
        TCGv t0;
        t0 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        tcg_gen_ext_tl_i64(src1, t0);
        tcg_temp_free(t0);
    }
    shift = tcg_const_tl(a->shamt);
    fn(dst, cpu_env, src1, shift);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free_i64(src1);
    tcg_temp_free(shift);
    tcg_temp_free(dst);
    return true;
}

#define GEN_RVP_SHIFTI_S64_OOL(NAME, OP)                    \
static bool trans_##NAME(DisasContext *s, arg_shift *a)     \
{                                                           \
    return rvp_shifti_s64_ool(s, a, gen_helper_##OP);       \
}

GEN_RVP_SHIFTI_S64_OOL(wexti, wext);

typedef void gen_helper_rvp_r4(TCGv, TCGv_ptr, TCGv, TCGv, TCGv);

static bool r4_ool(DisasContext *ctx, arg_r4 *a, gen_helper_rvp_r4 *fn)
{
    TCGv src1, src2, src3, dst;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    src3 = tcg_temp_new();
    dst = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    gen_get_gpr(src3, a->rs3);
    fn(dst, cpu_env, src1, src2, src3);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free(src3);
    tcg_temp_free(dst);
    return true;
}

#define GEN_RVP_R4_OOL(NAME)                           \
static bool trans_##NAME(DisasContext *s, arg_r4 *a)   \
{                                                      \
    return r4_ool(s, a, gen_helper_##NAME);            \
}

GEN_RVP_R4_OOL(bpick);

static bool trans_insb(DisasContext *ctx, arg_shift *a)
{
    TCGv src1, dst, b0;
    uint8_t shift;
    if (!has_ext(ctx, RVP)) {
        return false;
    }
    if (is_32bit(ctx)) {
        shift = a->shamt & 0x3;
    } else {
        shift = a->shamt;
    }
    src1 = tcg_temp_new();
    dst = tcg_temp_new();
    b0 = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(dst, a->rd);

    tcg_gen_andi_tl(b0, src1, 0xff);
    tcg_gen_deposit_tl(dst, dst, b0, shift * 8, 8);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(dst);
    tcg_temp_free(b0);
    return true;
}

static bool trans_maddr32(DisasContext *ctx, arg_r *a)
{
    TCGv src1, src2, dst;
    TCGv_i32 w1, w2, w3;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    dst = tcg_temp_new();
    w1 = tcg_temp_new_i32();
    w2 = tcg_temp_new_i32();
    w3 = tcg_temp_new_i32();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    gen_get_gpr(dst, a->rd);

    tcg_gen_trunc_tl_i32(w1, src1);
    tcg_gen_trunc_tl_i32(w2, src2);
    tcg_gen_trunc_tl_i32(w3, dst);

    tcg_gen_mul_i32(w1, w1, w2);
    tcg_gen_add_i32(w3, w3, w1);
    tcg_gen_ext_i32_tl(dst, w3);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free(dst);
    tcg_temp_free_i32(w1);
    tcg_temp_free_i32(w2);
    tcg_temp_free_i32(w3);
    return true;
}

static bool trans_msubr32(DisasContext *ctx, arg_r *a)
{
    TCGv src1, src2, dst;
    TCGv_i32 w1, w2, w3;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    dst = tcg_temp_new();
    w1 = tcg_temp_new_i32();
    w2 = tcg_temp_new_i32();
    w3 = tcg_temp_new_i32();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    gen_get_gpr(dst, a->rd);

    tcg_gen_trunc_tl_i32(w1, src1);
    tcg_gen_trunc_tl_i32(w2, src2);
    tcg_gen_trunc_tl_i32(w3, dst);

    tcg_gen_mul_i32(w1, w1, w2);
    tcg_gen_sub_i32(w3, w3, w1);
    tcg_gen_ext_i32_tl(dst, w3);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free(dst);
    tcg_temp_free_i32(w1);
    tcg_temp_free_i32(w2);
    tcg_temp_free_i32(w3);
    return true;
}

/*
 *** RV64 Only Instructions
 */
/* RV64 Only) SIMD 32-bit Add/Subtract Instructions */
#define GEN_RVP64_R_INLINE(NAME, VECOP, OP)              \
static bool trans_##NAME(DisasContext *s, arg_r *a)      \
{                                                        \
    REQUIRE_64BIT(s);                                    \
    return r_inline(s, a, VECOP, OP);                    \
}

GEN_RVP64_R_INLINE(add32, tcg_gen_vec_add32_tl, tcg_gen_add_tl);
GEN_RVP64_R_INLINE(sub32, tcg_gen_vec_sub32_tl, tcg_gen_sub_tl);

static bool
r_64_ool(DisasContext *ctx, arg_r *a,
         void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv t1, t2;
    TCGv_i64 src1, src2, dst;

    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    gen_get_gpr(t1, a->rs1);
    tcg_gen_ext_tl_i64(src1, t1);
    gen_get_gpr(t2, a->rs2);
    tcg_gen_ext_tl_i64(src2, t2);

    fn(dst, cpu_env, src1, src2);
    tcg_gen_trunc_i64_tl(t1, dst);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_temp_free_i64(src1);
    tcg_temp_free_i64(src2);
    tcg_temp_free_i64(dst);
    return true;
}

#define GEN_RVP64_R_OOL(NAME)                          \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    REQUIRE_64BIT(s);                                  \
    return r_64_ool(s, a, gen_helper_##NAME);          \
}

GEN_RVP64_R_OOL(radd32);
GEN_RVP64_R_OOL(uradd32);
GEN_RVP64_R_OOL(kadd32);
GEN_RVP64_R_OOL(ukadd32);
GEN_RVP64_R_OOL(rsub32);
GEN_RVP64_R_OOL(ursub32);
GEN_RVP64_R_OOL(ksub32);
GEN_RVP64_R_OOL(uksub32);
GEN_RVP64_R_OOL(cras32);
GEN_RVP64_R_OOL(rcras32);
GEN_RVP64_R_OOL(urcras32);
GEN_RVP64_R_OOL(kcras32);
GEN_RVP64_R_OOL(ukcras32);
GEN_RVP64_R_OOL(crsa32);
GEN_RVP64_R_OOL(rcrsa32);
GEN_RVP64_R_OOL(urcrsa32);
GEN_RVP64_R_OOL(kcrsa32);
GEN_RVP64_R_OOL(ukcrsa32);
GEN_RVP64_R_OOL(stas32);
GEN_RVP64_R_OOL(rstas32);
GEN_RVP64_R_OOL(urstas32);
GEN_RVP64_R_OOL(kstas32);
GEN_RVP64_R_OOL(ukstas32);
GEN_RVP64_R_OOL(stsa32);
GEN_RVP64_R_OOL(rstsa32);
GEN_RVP64_R_OOL(urstsa32);
GEN_RVP64_R_OOL(kstsa32);
GEN_RVP64_R_OOL(ukstsa32);

/* (RV64 Only) SIMD 32-bit Shift Instructions */
static inline bool
rvp64_shifti(DisasContext *ctx, arg_shift *a,
             void (* fn)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv t1;
    TCGv_i64 src1, dst, shift;
    if (!has_ext(ctx, RVP)) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();
    t1 = tcg_temp_new();

    gen_get_gpr(t1, a->rs1);
    tcg_gen_ext_tl_i64(src1, t1);
    shift = tcg_const_i64(a->shamt);

    fn(dst, cpu_env, src1, shift);
    tcg_gen_trunc_i64_tl(t1, dst);
    gen_set_gpr(a->rd, t1);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i64(dst);
    tcg_temp_free_i64(shift);
    tcg_temp_free(t1);
    return true;
}

#define GEN_RVP64_SHIFTI(NAME, OP)                       \
static bool trans_##NAME(DisasContext *s, arg_shift *a)  \
{                                                        \
    REQUIRE_64BIT(s);                                    \
    return rvp64_shifti(s, a, OP);                       \
}

GEN_RVP64_SHIFTI(srai32, gen_helper_sra32);
GEN_RVP64_SHIFTI(srli32, gen_helper_srl32);
GEN_RVP64_SHIFTI(slli32, gen_helper_sll32);

GEN_RVP64_SHIFTI(srai32_u, gen_helper_sra32_u);
GEN_RVP64_SHIFTI(srli32_u, gen_helper_srl32_u);
GEN_RVP64_SHIFTI(kslli32, gen_helper_ksll32);

GEN_RVP64_R_OOL(sra32);
GEN_RVP64_R_OOL(srl32);
GEN_RVP64_R_OOL(sll32);
GEN_RVP64_R_OOL(ksll32);
GEN_RVP64_R_OOL(kslra32);

GEN_RVP64_R_OOL(sra32_u);
GEN_RVP64_R_OOL(srl32_u);
GEN_RVP64_R_OOL(kslra32_u);
