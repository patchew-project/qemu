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
