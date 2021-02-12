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
