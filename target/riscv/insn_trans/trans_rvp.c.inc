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
