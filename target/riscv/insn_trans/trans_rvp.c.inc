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
 * 2) Otherwise, it can be acclebrated by an gvec op or an inline op.
 */

typedef void GenZeroFn(DisasContext *, arg_r *);
typedef void GenNoZero32Fn(TCGv, TCGv, TCGv);
typedef void GenNoZero64Fn(unsigned, uint32_t, uint32_t,
                           uint32_t, uint32_t, uint32_t);

static inline bool
r_inline(DisasContext *ctx, arg_r *a, uint8_t vece,
         GenNoZero64Fn *f64, GenNoZero32Fn *f32,
         GenZeroFn *fn)
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }
    if (a->rd && a->rs1 && a->rs2) {
#ifdef TARGET_RISCV64
        f64(vece, offsetof(CPURISCVState, gpr[a->rd]),
            offsetof(CPURISCVState, gpr[a->rs1]),
            offsetof(CPURISCVState, gpr[a->rs2]),
            8, 8);
#else
        f32(cpu_gpr[a->rd], cpu_gpr[a->rs1], cpu_gpr[a->rs2]);
#endif
    } else {
        fn(ctx, a);
    }
    return true;
}

/* Complete inline implementation */
#define GEN_RVP_R_INLINE(NAME, GSUF, VECE, FN)                     \
static bool trans_##NAME(DisasContext *s, arg_r *a)                \
{                                                                  \
    return r_inline(s, a, VECE, tcg_gen_gvec_##GSUF,               \
                    tcg_gen_simd_##NAME, (GenZeroFn *)FN);         \
}                                                                  \

static void tcg_gen_simd_add16(TCGv d, TCGv a, TCGv b)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    tcg_gen_andi_tl(t1, a, ~0xffff);
    tcg_gen_add_tl(t2, a, b);
    tcg_gen_add_tl(t1, t1, b);
    tcg_gen_deposit_tl(d, t1, t2, 0, 16);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

GEN_RVP_R_INLINE(add16, add, 1, trans_add);

static void tcg_gen_simd_sub16(TCGv d, TCGv a, TCGv b)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    tcg_gen_andi_tl(t1, b, ~0xffff);
    tcg_gen_sub_tl(t2, a, b);
    tcg_gen_sub_tl(t1, a, t1);
    tcg_gen_deposit_tl(d, t1, t2, 0, 16);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

GEN_RVP_R_INLINE(sub16, sub, 1, trans_sub);

/* Out of line helpers for R format packed instructions */
typedef void gen_helper_rvp_r(TCGv, TCGv_ptr, TCGv, TCGv);

static inline bool r_ool(DisasContext *ctx, arg_r *a, gen_helper_rvp_r *fn)
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
/*
 *  Copied from tcg-op-gvec.c.
 *
 *  Perform a vector addition using normal addition and a mask.  The mask
 *  should be the sign bit of each lane.  This 6-operation form is more
 *  efficient than separate additions when there are 4 or more lanes in
 *  the 64-bit operation.
 */

static void gen_simd_add_mask(TCGv d, TCGv a, TCGv b, TCGv m)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    tcg_gen_andc_tl(t1, a, m);
    tcg_gen_andc_tl(t2, b, m);
    tcg_gen_xor_tl(t3, a, b);
    tcg_gen_add_tl(d, t1, t2);
    tcg_gen_and_tl(t3, t3, m);
    tcg_gen_xor_tl(d, d, t3);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_temp_free(t3);
}

static void tcg_gen_simd_add8(TCGv d, TCGv a, TCGv b)
{
    TCGv m = tcg_const_tl((target_ulong)dup_const(MO_8, 0x80));
    gen_simd_add_mask(d, a, b, m);
    tcg_temp_free(m);
}

GEN_RVP_R_INLINE(add8, add, 0, trans_add);

/*
 *  Copied from tcg-op-gvec.c.
 *
 *  Perform a vector subtraction using normal subtraction and a mask.
 *  Compare gen_addv_mask above.
 */
static void gen_simd_sub_mask(TCGv d, TCGv a, TCGv b, TCGv m)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    tcg_gen_or_tl(t1, a, m);
    tcg_gen_andc_tl(t2, b, m);
    tcg_gen_eqv_tl(t3, a, b);
    tcg_gen_sub_tl(d, t1, t2);
    tcg_gen_and_tl(t3, t3, m);
    tcg_gen_xor_tl(d, d, t3);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_temp_free(t3);
}

static void tcg_gen_simd_sub8(TCGv d, TCGv a, TCGv b)
{
    TCGv m = tcg_const_tl((target_ulong)dup_const(MO_8, 0x80));
    gen_simd_sub_mask(d, a, b, m);
    tcg_temp_free(m);
}

GEN_RVP_R_INLINE(sub8, sub, 0, trans_sub);

GEN_RVP_R_OOL(radd8);
GEN_RVP_R_OOL(uradd8);
GEN_RVP_R_OOL(kadd8);
GEN_RVP_R_OOL(ukadd8);
GEN_RVP_R_OOL(rsub8);
GEN_RVP_R_OOL(ursub8);
GEN_RVP_R_OOL(ksub8);
GEN_RVP_R_OOL(uksub8);

/* 16-bit Shift Instructions */
static bool rvp_shift_ool(DisasContext *ctx, arg_r *a,
                          gen_helper_rvp_r *fn, target_ulong mask)
{
    TCGv src1, src2, dst;

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    dst = tcg_temp_new();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    tcg_gen_andi_tl(src2, src2, mask);

    fn(dst, cpu_env, src1, src2);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free(dst);
    return true;
}

typedef void GenGvecShift(unsigned, uint32_t, uint32_t, TCGv_i32,
                          uint32_t, uint32_t);
static inline bool
rvp_shift(DisasContext *ctx, arg_r *a, uint8_t vece,
          GenGvecShift *f64, gen_helper_rvp_r *fn,
          uint8_t mask)
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }

#ifdef TARGET_RISCV64
    if (a->rd && a->rs1 && a->rs2) {
        TCGv_i32 shift = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(shift, cpu_gpr[a->rs2]);
        tcg_gen_andi_i32(shift, shift, mask);
        f64(vece, offsetof(CPURISCVState, gpr[a->rd]),
            offsetof(CPURISCVState, gpr[a->rs1]),
            shift, 8, 8);
        tcg_temp_free_i32(shift);
        return true;
    }
#endif
    return rvp_shift_ool(ctx, a, fn, mask);
}

#define GEN_RVP_SHIFT(NAME, GVEC, VECE)                     \
static bool trans_##NAME(DisasContext *s, arg_r *a)         \
{                                                           \
    return rvp_shift(s, a, VECE, GVEC, gen_helper_##NAME,   \
                     (8 << VECE) - 1);                      \
}

GEN_RVP_SHIFT(sra16, tcg_gen_gvec_sars, 1);
GEN_RVP_SHIFT(srl16, tcg_gen_gvec_shrs, 1);
GEN_RVP_SHIFT(sll16, tcg_gen_gvec_shls, 1);
GEN_RVP_R_OOL(sra16_u);
GEN_RVP_R_OOL(srl16_u);
GEN_RVP_R_OOL(ksll16);
GEN_RVP_R_OOL(kslra16);
GEN_RVP_R_OOL(kslra16_u);

static bool rvp_shifti_ool(DisasContext *ctx, arg_shift *a,
                           gen_helper_rvp_r *fn)
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
           void (* f64)(TCGv_i64, TCGv_i64, int64_t),
           gen_helper_rvp_r *fn)
{
    if (!has_ext(ctx, RVP)) {
        return false;
    }

#ifdef TARGET_RISCV64
    if (a->rd && a->rs1 && f64) {
        f64(cpu_gpr[a->rd], cpu_gpr[a->rs1], a->shamt);
        return true;
    }
#endif
    return rvp_shifti_ool(ctx, a, fn);
}

#define GEN_RVP_SHIFTI(NAME, OP, GVEC)                   \
static bool trans_##NAME(DisasContext *s, arg_shift *a)  \
{                                                        \
    return rvp_shifti(s, a, GVEC, gen_helper_##OP);      \
}

GEN_RVP_SHIFTI(srai16, sra16, tcg_gen_vec_sar16i_i64);
GEN_RVP_SHIFTI(srli16, srl16, tcg_gen_vec_shr16i_i64);
GEN_RVP_SHIFTI(slli16, sll16, tcg_gen_vec_shl16i_i64);
GEN_RVP_SHIFTI(srai16_u, sra16_u, NULL);
GEN_RVP_SHIFTI(srli16_u, srl16_u, NULL);
GEN_RVP_SHIFTI(kslli16, ksll16, NULL);

/* SIMD 8-bit Shift Instructions */
GEN_RVP_SHIFT(sra8, tcg_gen_gvec_sars, 0);
GEN_RVP_SHIFT(srl8, tcg_gen_gvec_shrs, 0);
GEN_RVP_SHIFT(sll8, tcg_gen_gvec_shls, 0);
GEN_RVP_R_OOL(sra8_u);
GEN_RVP_R_OOL(srl8_u);
GEN_RVP_R_OOL(ksll8);
GEN_RVP_R_OOL(kslra8);
GEN_RVP_R_OOL(kslra8_u);
GEN_RVP_SHIFTI(srai8, sra8, tcg_gen_vec_sar8i_i64);
GEN_RVP_SHIFTI(srli8, srl8, tcg_gen_vec_shr8i_i64);
GEN_RVP_SHIFTI(slli8, sll8, tcg_gen_vec_shl8i_i64);
GEN_RVP_SHIFTI(srai8_u, sra8_u, NULL);
GEN_RVP_SHIFTI(srli8_u, srl8_u, NULL);
GEN_RVP_SHIFTI(kslli8, ksll8, NULL);

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
static inline bool
r_d64_ool(DisasContext *ctx, arg_r *a,
          void (* fn)(TCGv_i64, TCGv_ptr, TCGv, TCGv))
{
#ifdef TARGET_RISCV64
    return r_ool(ctx, a, fn);
#else
    TCGv src1, src2;
    TCGv_i64 dst;
    TCGv_i32 low, high;

    if (!has_ext(ctx, RVP) || !ctx->ext_p64) {
        return false;
    }

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    dst = tcg_temp_new_i64();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    fn(dst, cpu_env, src1, src2);

    low = tcg_temp_new_i32();
    high = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(low, dst);
    tcg_gen_extrh_i64_i32(high, dst);
    gen_set_gpr(a->rd, low);
    gen_set_gpr(a->rd + 1, high);
    tcg_temp_free_i32(low);
    tcg_temp_free_i32(high);

    tcg_temp_free(src1);
    tcg_temp_free(src2);
    tcg_temp_free_i64(dst);
    return true;
#endif
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
GEN_RVP_SHIFTI(sclip16, sclip16, NULL);
GEN_RVP_SHIFTI(uclip16, uclip16, NULL);

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
GEN_RVP_R2_OOL(swap16);

/* SIMD 8-bit Miscellaneous Instructions */
GEN_RVP_R_OOL(smin8);
GEN_RVP_R_OOL(umin8);
GEN_RVP_R_OOL(smax8);
GEN_RVP_R_OOL(umax8);
GEN_RVP_SHIFTI(sclip8, sclip8, NULL);
GEN_RVP_SHIFTI(uclip8, uclip8, NULL);
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

/* Most Significant Word "32x32" Multiply & Add Instructions */
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

/* Most Significant Word "32x16" Multiply & Add Instructions */
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
#ifdef TARGET_RISCV64
    return r_ool(ctx, a, fn);
#else
    TCGv_i32 src2, a0, a1, d0, d1;
    TCGv_i64 src1, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_p64) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i32();
    dst = tcg_temp_new_i64();
    a0 = tcg_temp_new_i32();
    a1 = tcg_temp_new_i32();

    gen_get_gpr(a0, a->rs1);
    gen_get_gpr(a1, a->rs1 + 1);
    tcg_gen_concat_i32_i64(src1, a0, a1);
    gen_get_gpr(src2, a->rs2);

    fn(dst, cpu_env, src1, src2);

    d0 = tcg_temp_new_i32();
    d1 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(d0, dst);
    tcg_gen_extrh_i64_i32(d1, dst);
    gen_set_gpr(a->rd, d0);
    gen_set_gpr(a->rd + 1, d1);
    tcg_temp_free_i32(d0);
    tcg_temp_free_i32(d1);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i32(src2);
    tcg_temp_free_i64(dst);
    tcg_temp_free_i32(a0);
    tcg_temp_free_i32(a1);
    return true;
#endif
}

#define GEN_RVP_R_D64_S64_OOL(NAME)                    \
static bool trans_##NAME(DisasContext *s, arg_r *a)    \
{                                                      \
    return r_d64_s64_ool(s, a, gen_helper_##NAME);     \
}

GEN_RVP_R_D64_S64_OOL(smal);

/* Partial-SIMD Miscellaneous Instructions */
GEN_RVP_SHIFTI(sclip32, sclip32, NULL);
GEN_RVP_SHIFTI(uclip32, uclip32, NULL);
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
#ifdef TARGET_RISCV64
    return r_ool(ctx, a, fn);
#else
    TCGv a0, a1, b0, b1, d0, d1;
    TCGv_i64 src1, src2, dst;

    if (!has_ext(ctx, RVP) || !ctx->ext_p64) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();
    a0 = tcg_temp_new_i32();
    a1 = tcg_temp_new_i32();
    b0 = tcg_temp_new_i32();
    b1 = tcg_temp_new_i32();

    gen_get_gpr(a0, a->rs1);
    gen_get_gpr(a1, a->rs1 + 1);
    tcg_gen_concat_i32_i64(src1, a0, a1);
    gen_get_gpr(b0, a->rs2);
    gen_get_gpr(b1, a->rs2 + 1);
    tcg_gen_concat_i32_i64(src2, b0, b1);

    fn(dst, cpu_env, src1, src2);

    d0 = tcg_temp_new_i32();
    d1 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(d0, dst);
    tcg_gen_extrh_i64_i32(d1, dst);
    gen_set_gpr(a->rd, d0);
    gen_set_gpr(a->rd + 1, d1);
    tcg_temp_free_i32(d0);
    tcg_temp_free_i32(d1);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i64(src2);
    tcg_temp_free_i64(dst);
    tcg_temp_free_i32(a0);
    tcg_temp_free_i32(a1);
    tcg_temp_free_i32(b0);
    tcg_temp_free_i32(b1);
    return true;
#endif
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
#ifdef TARGET_RISCV64
    return r_acc_ool(ctx, a, fn);
#else
    TCGv_i32 src1, src2;
    TCGv_i64 dst, src3;
    TCGv_i32 d0, d1;

    if (!has_ext(ctx, RVP) || !ctx->ext_p64) {
        return false;
    }

    src1 = tcg_temp_new_i32();
    src2 = tcg_temp_new_i32();
    src3 = tcg_temp_new_i64();
    dst = tcg_temp_new_i64();
    d0 = tcg_temp_new_i32();
    d1 = tcg_temp_new_i32();

    gen_get_gpr(src1, a->rs1);
    gen_get_gpr(src2, a->rs2);
    gen_get_gpr(d0, a->rd);
    gen_get_gpr(d1, a->rd + 1);
    tcg_gen_concat_i32_i64(src3, d0, d1);

    fn(dst, cpu_env, src1, src2, src3);

    tcg_gen_extrl_i64_i32(d0, dst);
    tcg_gen_extrh_i64_i32(d1, dst);
    gen_set_gpr(a->rd, d0);
    gen_set_gpr(a->rd + 1, d1);

    tcg_temp_free_i32(d0);
    tcg_temp_free_i32(d1);
    tcg_temp_free_i32(src1);
    tcg_temp_free_i32(src2);
    tcg_temp_free_i64(src3);
    tcg_temp_free_i64(dst);
    return true;
#endif
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
GEN_RVP_SHIFTI(kslliw, ksllw, NULL);
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
GEN_RVP_SHIFTI(srai_u, sra_u, NULL);
GEN_RVP_R_OOL(bitrev);
GEN_RVP_SHIFTI(bitrevi, bitrev, NULL);

static bool
r_s64_ool(DisasContext *ctx, arg_r *a,
          void (* fn)(TCGv, TCGv_ptr, TCGv_i64, TCGv))
{
#ifdef TARGET_RISCV64
    return r_ool(ctx, a, fn);
#else
    TCGv_i64 src1;
    TCGv_i32 src2, dst;
    TCGv_i32 a0, a1;

    if (!has_ext(ctx, RVP) || !ctx->ext_p64) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i32();
    a0 = tcg_temp_new_i32();
    a1 = tcg_temp_new_i32();
    dst = tcg_temp_new_i32();

    gen_get_gpr(a0, a->rs1);
    gen_get_gpr(a1, a->rs1 + 1);
    tcg_gen_concat_i32_i64(src1, a0, a1);
    gen_get_gpr(src2, a->rs2);
    fn(dst, cpu_env, src1, src2);

    gen_set_gpr(a->rd, dst);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i32(a0);
    tcg_temp_free_i32(a1);
    tcg_temp_free_i32(src2);
    tcg_temp_free_i32(dst);
    return true;
#endif
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
#ifdef TARGET_RISCV64
    return rvp_shifti_ool(ctx, a, fn);
#else
    TCGv_i64 src1;
    TCGv_i32 shift, dst;
    TCGv_i32 a0, a1;

    if (!has_ext(ctx, RVP) || !ctx->ext_p64) {
        return false;
    }

    src1 = tcg_temp_new_i64();
    a0 = tcg_temp_new_i32();
    a1 = tcg_temp_new_i32();
    dst = tcg_temp_new_i32();

    gen_get_gpr(a0, a->rs1);
    gen_get_gpr(a1, a->rs1 + 1);
    tcg_gen_concat_i32_i64(src1, a0, a1);
    shift = tcg_const_tl(a->shamt);
    fn(dst, cpu_env, src1, shift);
    gen_set_gpr(a->rd, dst);

    tcg_temp_free_i64(src1);
    tcg_temp_free_i32(a0);
    tcg_temp_free_i32(a1);
    tcg_temp_free_i32(shift);
    tcg_temp_free_i32(dst);
    return true;
#endif
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
#ifdef TARGET_RISCV32
    shift = a->shamt & 0x3;
#else
    shift = a->shamt;
#endif
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

#ifdef TARGET_RISCV64
/*
 *** RV64 Only Instructions
 */
/* RV64 Only) SIMD 32-bit Add/Subtract Instructions */
static void tcg_gen_simd_add32(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_andi_i64(t1, a, ~0xffffffff);
    tcg_gen_add_i64(t2, a, b);
    tcg_gen_add_i64(t1, t1, b);
    tcg_gen_deposit_i64(d, t1, t2, 0, 32);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

GEN_RVP_R_INLINE(add32, add, 2, trans_add);

static void tcg_gen_simd_sub32(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_andi_i64(t1, b, ~0xffffffff);
    tcg_gen_sub_i64(t2, a, b);
    tcg_gen_sub_i64(t1, a, t1);
    tcg_gen_deposit_i64(d, t1, t2, 0, 32);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

GEN_RVP_R_INLINE(sub32, sub, 2, trans_sub);

GEN_RVP_R_OOL(radd32);
GEN_RVP_R_OOL(uradd32);
GEN_RVP_R_OOL(kadd32);
GEN_RVP_R_OOL(ukadd32);
GEN_RVP_R_OOL(rsub32);
GEN_RVP_R_OOL(ursub32);
GEN_RVP_R_OOL(ksub32);
GEN_RVP_R_OOL(uksub32);
GEN_RVP_R_OOL(cras32);
GEN_RVP_R_OOL(rcras32);
GEN_RVP_R_OOL(urcras32);
GEN_RVP_R_OOL(kcras32);
GEN_RVP_R_OOL(ukcras32);
GEN_RVP_R_OOL(crsa32);
GEN_RVP_R_OOL(rcrsa32);
GEN_RVP_R_OOL(urcrsa32);
GEN_RVP_R_OOL(kcrsa32);
GEN_RVP_R_OOL(ukcrsa32);
GEN_RVP_R_OOL(stas32);
GEN_RVP_R_OOL(rstas32);
GEN_RVP_R_OOL(urstas32);
GEN_RVP_R_OOL(kstas32);
GEN_RVP_R_OOL(ukstas32);
GEN_RVP_R_OOL(stsa32);
GEN_RVP_R_OOL(rstsa32);
GEN_RVP_R_OOL(urstsa32);
GEN_RVP_R_OOL(kstsa32);
GEN_RVP_R_OOL(ukstsa32);

/* (RV64 Only) SIMD 32-bit Shift Instructions */
GEN_RVP_SHIFT(sra32, tcg_gen_gvec_sars, 2);
GEN_RVP_SHIFTI(srai32, sra32, NULL);
GEN_RVP_R_OOL(sra32_u);
GEN_RVP_SHIFTI(srai32_u, sra32_u, NULL);
GEN_RVP_SHIFT(srl32, tcg_gen_gvec_shrs, 2);
GEN_RVP_SHIFTI(srli32, srl32, NULL);
GEN_RVP_R_OOL(srl32_u);
GEN_RVP_SHIFTI(srli32_u, srl32_u, NULL);
GEN_RVP_SHIFT(sll32, tcg_gen_gvec_shls, 2);
GEN_RVP_SHIFTI(slli32, sll32, NULL);
GEN_RVP_R_OOL(ksll32);
GEN_RVP_SHIFTI(kslli32, ksll32, NULL);
GEN_RVP_R_OOL(kslra32);
GEN_RVP_R_OOL(kslra32_u);

/* (RV64 Only) SIMD 32-bit Miscellaneous Instructions */
GEN_RVP_R_OOL(smin32);
GEN_RVP_R_OOL(umin32);
GEN_RVP_R_OOL(smax32);
GEN_RVP_R_OOL(umax32);
GEN_RVP_R2_OOL(kabs32);

/* (RV64 Only) SIMD Q15 saturating Multiply Instructions */
GEN_RVP_R_OOL(khmbb16);
GEN_RVP_R_OOL(khmbt16);
GEN_RVP_R_OOL(khmtt16);
GEN_RVP_R_OOL(kdmbb16);
GEN_RVP_R_OOL(kdmbt16);
GEN_RVP_R_OOL(kdmtt16);
GEN_RVP_R_ACC_OOL(kdmabb16);
GEN_RVP_R_ACC_OOL(kdmabt16);
GEN_RVP_R_ACC_OOL(kdmatt16);

/* (RV64 Only) 32-bit Multiply Instructions */
GEN_RVP_R_OOL(smbt32);
GEN_RVP_R_OOL(smtt32);

/* (RV64 Only) 32-bit Multiply & Add Instructions */
GEN_RVP_R_ACC_OOL(kmabb32);
GEN_RVP_R_ACC_OOL(kmabt32);
GEN_RVP_R_ACC_OOL(kmatt32);
#endif
