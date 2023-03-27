/*
 * RISC-V FPU Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "internals.h"

target_ulong riscv_cpu_get_fflags(CPURISCVState *env)
{
    int soft = get_float_exception_flags(&env->fp_status);
    target_ulong hard = 0;

    hard |= (soft & float_flag_inexact) ? FPEXC_NX : 0;
    hard |= (soft & float_flag_underflow) ? FPEXC_UF : 0;
    hard |= (soft & float_flag_overflow) ? FPEXC_OF : 0;
    hard |= (soft & float_flag_divbyzero) ? FPEXC_DZ : 0;
    hard |= (soft & float_flag_invalid) ? FPEXC_NV : 0;

    return hard;
}

void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong hard)
{
    int soft = 0;

    soft |= (hard & FPEXC_NX) ? float_flag_inexact : 0;
    soft |= (hard & FPEXC_UF) ? float_flag_underflow : 0;
    soft |= (hard & FPEXC_OF) ? float_flag_overflow : 0;
    soft |= (hard & FPEXC_DZ) ? float_flag_divbyzero : 0;
    soft |= (hard & FPEXC_NV) ? float_flag_invalid : 0;

    set_float_exception_flags(soft, &env->fp_status);
}

void helper_set_rounding_mode(CPURISCVState *env, uint32_t rm)
{
    int softrm;

    if (rm == RISCV_FRM_DYN) {
        rm = env->frm;
    }
    switch (rm) {
    case RISCV_FRM_RNE:
        softrm = float_round_nearest_even;
        break;
    case RISCV_FRM_RTZ:
        softrm = float_round_to_zero;
        break;
    case RISCV_FRM_RDN:
        softrm = float_round_down;
        break;
    case RISCV_FRM_RUP:
        softrm = float_round_up;
        break;
    case RISCV_FRM_RMM:
        softrm = float_round_ties_away;
        break;
    default:
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    set_float_rounding_mode(softrm, &env->fp_status);
}

void helper_set_rounding_mode_chkfrm(CPURISCVState *env, uint32_t rm)
{
    int softrm;

    /* Always validate frm, even if rm != DYN. */
    if (unlikely(env->frm >= 5)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    if (rm == RISCV_FRM_DYN) {
        rm = env->frm;
    }
    switch (rm) {
    case RISCV_FRM_RNE:
        softrm = float_round_nearest_even;
        break;
    case RISCV_FRM_RTZ:
        softrm = float_round_to_zero;
        break;
    case RISCV_FRM_RDN:
        softrm = float_round_down;
        break;
    case RISCV_FRM_RUP:
        softrm = float_round_up;
        break;
    case RISCV_FRM_RMM:
        softrm = float_round_ties_away;
        break;
    case RISCV_FRM_ROD:
        softrm = float_round_to_odd;
        break;
    default:
        g_assert_not_reached();
    }

    set_float_rounding_mode(softrm, &env->fp_status);
}

static uint64_t do_fmadd_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2,
                           uint64_t rs3, int flags)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    float16 frs3 = check_nanbox_h(env, rs3);
    return nanbox_h(env, float16_muladd(frs1, frs2, frs3, flags,
                                        &env->fp_status));
}

static uint64_t do_fmadd_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2,
                           uint64_t rs3, int flags)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    float32 frs3 = check_nanbox_s(env, rs3);
    return nanbox_s(env, float32_muladd(frs1, frs2, frs3, flags,
                                        &env->fp_status));
}

uint64_t helper_fmadd_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3, 0);
}

uint64_t helper_fmadd_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, 0, &env->fp_status);
}

uint64_t helper_fmadd_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3, 0);
}

uint64_t helper_fmsub_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3, float_muladd_negate_c);
}

uint64_t helper_fmsub_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, float_muladd_negate_c,
                          &env->fp_status);
}

uint64_t helper_fmsub_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                        uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3, float_muladd_negate_c);
}

uint64_t helper_fnmsub_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3, float_muladd_negate_product);
}

uint64_t helper_fnmsub_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, float_muladd_negate_product,
                          &env->fp_status);
}

uint64_t helper_fnmsub_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3, float_muladd_negate_product);
}

uint64_t helper_fnmadd_s(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_s(env, frs1, frs2, frs3,
                      float_muladd_negate_c | float_muladd_negate_product);
}

uint64_t helper_fnmadd_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return float64_muladd(frs1, frs2, frs3, float_muladd_negate_c |
                          float_muladd_negate_product, &env->fp_status);
}

uint64_t helper_fnmadd_h(CPURISCVState *env, uint64_t frs1, uint64_t frs2,
                         uint64_t frs3)
{
    return do_fmadd_h(env, frs1, frs2, frs3,
                      float_muladd_negate_c | float_muladd_negate_product);
}

uint64_t helper_fadd_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_add(frs1, frs2, &env->fp_status));
}

uint64_t helper_fsub_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_sub(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmul_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_mul(frs1, frs2, &env->fp_status));
}

uint64_t helper_fdiv_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, float32_div(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmin_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                    float32_minnum(frs1, frs2, &env->fp_status) :
                    float32_minimum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fminm_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);

    if (float32_is_any_nan(frs1) || float32_is_any_nan(frs2)) {
        return float32_default_nan(&env->fp_status);
    }

    return nanbox_s(env, float32_minimum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmax_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return nanbox_s(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                    float32_maxnum(frs1, frs2, &env->fp_status) :
                    float32_maximum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmaxm_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);

    if (float32_is_any_nan(frs1) || float32_is_any_nan(frs2)) {
        return float32_default_nan(&env->fp_status);
    }

    return nanbox_s(env, float32_maximum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fsqrt_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return nanbox_s(env, float32_sqrt(frs1, &env->fp_status));
}

target_ulong helper_fle_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_le(frs1, frs2, &env->fp_status);
}

target_ulong helper_fleq_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_le_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_flt_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_lt(frs1, frs2, &env->fp_status);
}

target_ulong helper_fltq_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_lt_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_feq_s(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    float32 frs2 = check_nanbox_s(env, rs2);
    return float32_eq_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_fcvt_w_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_int32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_wu_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return (int32_t)float32_to_uint32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_l_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_int64(frs1, &env->fp_status);
}

target_ulong helper_fcvt_lu_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_uint64(frs1, &env->fp_status);
}

uint64_t helper_fcvt_s_w(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, int32_to_float32((int32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_s_wu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, uint32_to_float32((uint32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_s_l(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, int64_to_float32(rs1, &env->fp_status));
}

uint64_t helper_fcvt_s_lu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_s(env, uint64_to_float32(rs1, &env->fp_status));
}

target_ulong helper_fclass_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return fclass_s(frs1);
}

uint64_t helper_fround_s(CPURISCVState *env, uint64_t frs1)
{
    if (float32_is_zero(frs1) ||
        float32_is_infinity(frs1)) {
        return frs1;
    }

    if (float32_is_any_nan(frs1)) {
        riscv_cpu_set_fflags(env, FPEXC_NV);
        return frs1;
    }

    int32_t tmp = float32_to_int32(frs1, &env->fp_status);
    return nanbox_s(env, int32_to_float32(tmp, &env->fp_status));
}

uint64_t helper_froundnx_s(CPURISCVState *env, uint64_t frs1)
{
    uint64_t ret = helper_fround_s(env, frs1);

    if (ret != frs1 && !float32_is_any_nan(frs1)) {
        riscv_cpu_set_fflags(env, FPEXC_NX);
    }

    return ret;
}

uint64_t helper_fli_s(CPURISCVState *env, uint32_t rs1)
{
    const uint32_t fli_s_table[] = {
        0xbf800000,  /* -1.0 */
        0x00800000,  /* minimum positive normal */
        0x37800000,  /* 1.0 * 2^-16 */
        0x38000000,  /* 1.0 * 2^-15 */
        0x3b800000,  /* 1.0 * 2^-8  */
        0x3c000000,  /* 1.0 * 2^-7  */
        0x3d800000,  /* 1.0 * 2^-4  */
        0x3e000000,  /* 1.0 * 2^-3  */
        0x3e800000,  /* 0.25 */
        0x3ea00000,  /* 0.3125 */
        0x3ec00000,  /* 0.375 */
        0x3ee00000,  /* 0.4375 */
        0x3f000000,  /* 0.5 */
        0x3f200000,  /* 0.625 */
        0x3f400000,  /* 0.75 */
        0x3f600000,  /* 0.875 */
        0x3f800000,  /* 1.0 */
        0x3fa00000,  /* 1.25 */
        0x3fc00000,  /* 1.5 */
        0x3fe00000,  /* 1.75 */
        0x40000000,  /* 2.0 */
        0x40200000,  /* 2.5 */
        0x40400000,  /* 3 */
        0x40800000,  /* 4 */
        0x41000000,  /* 8 */
        0x41800000,  /* 16 */
        0x43000000,  /* 2^7 */
        0x43800000,  /* 2^8 */
        0x47000000,  /* 2^15 */
        0x47800000,  /* 2^16 */
        0x7f800000,  /* +inf */
        float32_default_nan(&env->fp_status),
    };

    if (rs1 >= 32)
        g_assert_not_reached();

    return fli_s_table[rs1];
}

uint64_t helper_fadd_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_add(frs1, frs2, &env->fp_status);
}

uint64_t helper_fsub_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_sub(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmul_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_mul(frs1, frs2, &env->fp_status);
}

uint64_t helper_fdiv_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_div(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmin_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return env->priv_ver < PRIV_VERSION_1_11_0 ?
            float64_minnum(frs1, frs2, &env->fp_status) :
            float64_minimum_number(frs1, frs2, &env->fp_status);
}

uint64_t helper_fminm_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    if (float64_is_any_nan(frs1) || float64_is_any_nan(frs2)) {
        return float64_default_nan(&env->fp_status);
    }

    return float64_minimum_number(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmax_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return env->priv_ver < PRIV_VERSION_1_11_0 ?
            float64_maxnum(frs1, frs2, &env->fp_status) :
            float64_maximum_number(frs1, frs2, &env->fp_status);
}

uint64_t helper_fmaxm_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    if (float64_is_any_nan(frs1) || float64_is_any_nan(frs2)) {
        return float64_default_nan(&env->fp_status);
    }

    return float64_maximum_number(frs1, frs2, &env->fp_status);
}

uint64_t helper_fcvt_s_d(CPURISCVState *env, uint64_t rs1)
{
    return nanbox_s(env, float64_to_float32(rs1, &env->fp_status));
}

uint64_t helper_fcvt_d_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return float32_to_float64(frs1, &env->fp_status);
}

uint64_t helper_fsqrt_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_sqrt(frs1, &env->fp_status);
}

target_ulong helper_fle_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_le(frs1, frs2, &env->fp_status);
}

target_ulong helper_fleq_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_le_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_flt_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_lt(frs1, frs2, &env->fp_status);
}

target_ulong helper_fltq_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_lt_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_feq_d(CPURISCVState *env, uint64_t frs1, uint64_t frs2)
{
    return float64_eq_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_fcvt_w_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_to_int32(frs1, &env->fp_status);
}

target_ulong helper_fcvtmod_w_d(CPURISCVState *env, uint64_t frs1)
{
    if (float64_is_any_nan(frs1) ||
        float64_is_infinity(frs1)) {
        return 0;
    }

    return float64_to_int32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_wu_d(CPURISCVState *env, uint64_t frs1)
{
    return (int32_t)float64_to_uint32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_l_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_to_int64(frs1, &env->fp_status);
}

target_ulong helper_fcvt_lu_d(CPURISCVState *env, uint64_t frs1)
{
    return float64_to_uint64(frs1, &env->fp_status);
}

uint64_t helper_fcvt_d_w(CPURISCVState *env, target_ulong rs1)
{
    return int32_to_float64((int32_t)rs1, &env->fp_status);
}

uint64_t helper_fcvt_d_wu(CPURISCVState *env, target_ulong rs1)
{
    return uint32_to_float64((uint32_t)rs1, &env->fp_status);
}

uint64_t helper_fcvt_d_l(CPURISCVState *env, target_ulong rs1)
{
    return int64_to_float64(rs1, &env->fp_status);
}

uint64_t helper_fcvt_d_lu(CPURISCVState *env, target_ulong rs1)
{
    return uint64_to_float64(rs1, &env->fp_status);
}

target_ulong helper_fclass_d(uint64_t frs1)
{
    return fclass_d(frs1);
}

uint64_t helper_fround_d(CPURISCVState *env, uint64_t frs1)
{
    if (float64_is_zero(frs1) ||
        float64_is_infinity(frs1)) {
        return frs1;
    }

    if (float64_is_any_nan(frs1)) {
        riscv_cpu_set_fflags(env, FPEXC_NV);
        return frs1;
    }

    int64_t tmp = float64_to_int64(frs1, &env->fp_status);
    return nanbox_s(env, int64_to_float64(tmp, &env->fp_status));
}

uint64_t helper_froundnx_d(CPURISCVState *env, uint64_t frs1)
{
    uint64_t ret = helper_fround_s(env, frs1);

    if (ret != frs1 && !float64_is_any_nan(frs1)) {
        riscv_cpu_set_fflags(env, FPEXC_NX);
    }

    return ret;
}

uint64_t helper_fli_d(CPURISCVState *env, uint32_t rs1)
{
    const uint64_t fli_d_table[] = {
        0xbff0000000000000,  /* -1.0 */
        0x0010000000000000,  /* minimum positive normal */
        0x3Ef0000000000000,  /* 1.0 * 2^-16 */
        0x3f00000000000000,  /* 1.0 * 2^-15 */
        0x3f70000000000000,  /* 1.0 * 2^-8  */
        0x3f80000000000000,  /* 1.0 * 2^-7  */
        0x3fb0000000000000,  /* 1.0 * 2^-4  */
        0x3fc0000000000000,  /* 1.0 * 2^-3  */
        0x3fd0000000000000,  /* 0.25 */
        0x3fd4000000000000,  /* 0.3125 */
        0x3fd8000000000000,  /* 0.375 */
        0x3fdc000000000000,  /* 0.4375 */
        0x3fe0000000000000,  /* 0.5 */
        0x3fe4000000000000,  /* 0.625 */
        0x3fe8000000000000,  /* 0.75 */
        0x3fec000000000000,  /* 0.875 */
        0x3ff0000000000000,  /* 1.0 */
        0x3ff4000000000000,  /* 1.25 */
        0x3ff8000000000000,  /* 1.5 */
        0x3ffc000000000000,  /* 1.75 */
        0x4000000000000000,  /* 2.0 */
        0x4004000000000000,  /* 2.5 */
        0x4008000000000000,  /* 3 */
        0x4010000000000000,  /* 4 */
        0x4020000000000000,  /* 8 */
        0x4030000000000000,  /* 16 */
        0x4060000000000000,  /* 2^7 */
        0x4070000000000000,  /* 2^8 */
        0x40e0000000000000,  /* 2^15 */
        0x40f0000000000000,  /* 2^16 */
        0x7ff0000000000000,  /* +inf */
        float64_default_nan(&env->fp_status),
    };

    if (rs1 >= 32)
        g_assert_not_reached();

    return fli_d_table[rs1];
}

uint64_t helper_fadd_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_add(frs1, frs2, &env->fp_status));
}

uint64_t helper_fsub_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_sub(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmul_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_mul(frs1, frs2, &env->fp_status));
}

uint64_t helper_fdiv_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, float16_div(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmin_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                    float16_minnum(frs1, frs2, &env->fp_status) :
                    float16_minimum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fminm_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_s(env, rs1);
    float16 frs2 = check_nanbox_s(env, rs2);

    if (float16_is_any_nan(frs1) || float16_is_any_nan(frs2)) {
        return float16_default_nan(&env->fp_status);
    }

    return nanbox_s(env, float16_minimum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmax_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return nanbox_h(env, env->priv_ver < PRIV_VERSION_1_11_0 ?
                    float16_maxnum(frs1, frs2, &env->fp_status) :
                    float16_maximum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fmaxm_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_s(env, rs1);
    float16 frs2 = check_nanbox_s(env, rs2);

    if (float16_is_any_nan(frs1) || float16_is_any_nan(frs2)) {
        return float16_default_nan(&env->fp_status);
    }

    return nanbox_s(env, float16_maximum_number(frs1, frs2, &env->fp_status));
}

uint64_t helper_fsqrt_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return nanbox_h(env, float16_sqrt(frs1, &env->fp_status));
}

target_ulong helper_fle_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_le(frs1, frs2, &env->fp_status);
}

target_ulong helper_fleq_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_le_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_flt_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_lt(frs1, frs2, &env->fp_status);
}

target_ulong helper_fltq_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_lt_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_feq_h(CPURISCVState *env, uint64_t rs1, uint64_t rs2)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    float16 frs2 = check_nanbox_h(env, rs2);
    return float16_eq_quiet(frs1, frs2, &env->fp_status);
}

target_ulong helper_fclass_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return fclass_h(frs1);
}

uint64_t helper_fround_h(CPURISCVState *env, uint64_t frs1)
{
    if (float16_is_zero(frs1) ||
        float16_is_infinity(frs1)) {
        return frs1;
    }

    if (float16_is_any_nan(frs1)) {
        riscv_cpu_set_fflags(env, FPEXC_NV);
        return frs1;
    }

    int32_t tmp = float16_to_int32(frs1, &env->fp_status);
    return nanbox_s(env, int32_to_float16(tmp, &env->fp_status));
}

uint64_t helper_froundnx_h(CPURISCVState *env, uint64_t frs1)
{
    uint64_t ret = helper_fround_s(env, frs1);

    if (ret != frs1 && !float16_is_any_nan(frs1)) {
        riscv_cpu_set_fflags(env, FPEXC_NX);
    }

    return ret;
}

uint64_t helper_fli_h(CPURISCVState *env, uint32_t rs1)
{
    const uint16_t fli_h_table[] = {
        0xbc00,  /* -1.0 */
        0x0400,  /* minimum positive normal */
        0x0100,  /* 1.0 * 2^-16 */
        0x0200,  /* 1.0 * 2^-15 */
        0x1c00,  /* 1.0 * 2^-8  */
        0x2000,  /* 1.0 * 2^-7  */
        0x2c00,  /* 1.0 * 2^-4  */
        0x3000,  /* 1.0 * 2^-3  */
        0x3400,  /* 0.25 */
        0x3500,  /* 0.3125 */
        0x3600,  /* 0.375 */
        0x3700,  /* 0.4375 */
        0x3800,  /* 0.5 */
        0x3900,  /* 0.625 */
        0x3a00,  /* 0.75 */
        0x3b00,  /* 0.875 */
        0x3c00,  /* 1.0 */
        0x3d00,  /* 1.25 */
        0x3e00,  /* 1.5 */
        0x3f00,  /* 1.75 */
        0x4000,  /* 2.0 */
        0x4100,  /* 2.5 */
        0x4200,  /* 3 */
        0x4400,  /* 4 */
        0x4800,  /* 8 */
        0x4c00,  /* 16 */
        0x5800,  /* 2^7 */
        0x5c00,  /* 2^8 */
        0x7800,  /* 2^15 */
        0x7c00,  /* 2^16 */
        0x7c00,  /* +inf */
        float16_default_nan(&env->fp_status),
    };

    if (rs1 >= 32)
        g_assert_not_reached();

    return fli_h_table[rs1];
}

target_ulong helper_fcvt_w_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_int32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_wu_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return (int32_t)float16_to_uint32(frs1, &env->fp_status);
}

target_ulong helper_fcvt_l_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_int64(frs1, &env->fp_status);
}

target_ulong helper_fcvt_lu_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_uint64(frs1, &env->fp_status);
}

uint64_t helper_fcvt_h_w(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, int32_to_float16((int32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_wu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, uint32_to_float16((uint32_t)rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_l(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, int64_to_float16(rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_lu(CPURISCVState *env, target_ulong rs1)
{
    return nanbox_h(env, uint64_to_float16(rs1, &env->fp_status));
}

uint64_t helper_fcvt_h_s(CPURISCVState *env, uint64_t rs1)
{
    float32 frs1 = check_nanbox_s(env, rs1);
    return nanbox_h(env, float32_to_float16(frs1, true, &env->fp_status));
}

uint64_t helper_fcvt_s_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return nanbox_s(env, float16_to_float32(frs1, true, &env->fp_status));
}

uint64_t helper_fcvt_h_d(CPURISCVState *env, uint64_t rs1)
{
    return nanbox_h(env, float64_to_float16(rs1, true, &env->fp_status));
}

uint64_t helper_fcvt_d_h(CPURISCVState *env, uint64_t rs1)
{
    float16 frs1 = check_nanbox_h(env, rs1);
    return float16_to_float64(frs1, true, &env->fp_status);
}
