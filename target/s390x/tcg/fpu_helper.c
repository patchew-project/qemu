/*
 *  S/390 FPU helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static inline Int128 RET128(float128 f)
{
    return int128_make128(f.low, f.high);
}

static inline float128 ARG128(Int128 i)
{
    return make_float128(int128_gethi(i), int128_getlo(i));
}

uint8_t s390_softfloat_exc_to_ieee(unsigned int exc)
{
    uint8_t s390_exc = 0;

    s390_exc |= (exc & float_flag_invalid) ? S390_IEEE_MASK_INVALID : 0;
    s390_exc |= (exc & float_flag_divbyzero) ? S390_IEEE_MASK_DIVBYZERO : 0;
    s390_exc |= (exc & float_flag_overflow) ? S390_IEEE_MASK_OVERFLOW : 0;
    s390_exc |= (exc & float_flag_underflow) ? S390_IEEE_MASK_UNDERFLOW : 0;
    s390_exc |= (exc & (float_flag_inexact | float_flag_invalid_cvti)) ?
                S390_IEEE_MASK_INEXACT : 0;

    return s390_exc;
}

/* Should be called after any operation that may raise IEEE exceptions.  */
static void handle_exceptions(CPUS390XState *env, bool XxC, uintptr_t retaddr)
{
    unsigned s390_exc, qemu_exc;

    /* Get the exceptions raised by the current operation.  Reset the
       fpu_status contents so that the next operation has a clean slate.  */
    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return;
    }
    env->fpu_status.float_exception_flags = 0;
    s390_exc = s390_softfloat_exc_to_ieee(qemu_exc);

    /*
     * IEEE-Underflow exception recognition exists if a tininess condition
     * (underflow) exists and
     * - The mask bit in the FPC is zero and the result is inexact
     * - The mask bit in the FPC is one
     * So tininess conditions that are not inexact don't trigger any
     * underflow action in case the mask bit is not one.
     */
    if (!(s390_exc & S390_IEEE_MASK_INEXACT) &&
        !((env->fpc >> 24) & S390_IEEE_MASK_UNDERFLOW)) {
        s390_exc &= ~S390_IEEE_MASK_UNDERFLOW;
    }

    /*
     * FIXME:
     * 1. Right now, all inexact conditions are indicated as
     *    "truncated" (0) and never as "incremented" (1) in the DXC.
     * 2. Only traps due to invalid/divbyzero are suppressing. Other traps
     *    are completing, meaning the target register has to be written!
     *    This, however will mean that we have to write the register before
     *    triggering the trap - impossible right now.
     */

    /*
     * invalid/divbyzero cannot coexist with other conditions.
     * overflow/underflow however can coexist with inexact, we have to
     * handle it separately.
     */
    if (s390_exc & ~S390_IEEE_MASK_INEXACT) {
        if (s390_exc & ~S390_IEEE_MASK_INEXACT & env->fpc >> 24) {
            /* trap condition - inexact reported along */
            tcg_s390_data_exception(env, s390_exc, retaddr);
        }
        /* nontrap condition - inexact handled differently */
        env->fpc |= (s390_exc & ~S390_IEEE_MASK_INEXACT) << 16;
    }

    /* inexact handling */
    if (s390_exc & S390_IEEE_MASK_INEXACT && !XxC) {
        /* trap condition - overflow/underflow _not_ reported along */
        if (s390_exc & S390_IEEE_MASK_INEXACT & env->fpc >> 24) {
            tcg_s390_data_exception(env, s390_exc & S390_IEEE_MASK_INEXACT,
                                    retaddr);
        }
        /* nontrap condition */
        env->fpc |= (s390_exc & S390_IEEE_MASK_INEXACT) << 16;
    }
}

int float_comp_to_cc(CPUS390XState *env, FloatRelation float_compare)
{
    switch (float_compare) {
    case float_relation_equal:
        return 0;
    case float_relation_less:
        return 1;
    case float_relation_greater:
        return 2;
    case float_relation_unordered:
        return 3;
    default:
        cpu_abort(env_cpu(env), "unknown return value for float compare\n");
    }
}

/* condition codes for unary FP ops */
uint32_t set_cc_nz_f32(float32 v)
{
    if (float32_is_any_nan(v)) {
        return 3;
    } else if (float32_is_zero(v)) {
        return 0;
    } else if (float32_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

uint32_t set_cc_nz_f64(float64 v)
{
    if (float64_is_any_nan(v)) {
        return 3;
    } else if (float64_is_zero(v)) {
        return 0;
    } else if (float64_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

uint32_t set_cc_nz_f128(float128 v)
{
    if (float128_is_any_nan(v)) {
        return 3;
    } else if (float128_is_zero(v)) {
        return 0;
    } else if (float128_is_neg(v)) {
        return 1;
    } else {
        return 2;
    }
}

/* condition codes for FP to integer conversion ops */
static uint32_t set_cc_conv_f32(float32 v, float_status *stat)
{
    if (stat->float_exception_flags & float_flag_invalid) {
        return 3;
    } else {
        return set_cc_nz_f32(v);
    }
}

static uint32_t set_cc_conv_f64(float64 v, float_status *stat)
{
    if (stat->float_exception_flags & float_flag_invalid) {
        return 3;
    } else {
        return set_cc_nz_f64(v);
    }
}

static uint32_t set_cc_conv_f128(float128 v, float_status *stat)
{
    if (stat->float_exception_flags & float_flag_invalid) {
        return 3;
    } else {
        return set_cc_nz_f128(v);
    }
}

static inline uint8_t round_from_m34(uint32_t m34)
{
    return extract32(m34, 0, 4);
}

static inline bool xxc_from_m34(uint32_t m34)
{
    /* XxC is bit 1 of m4 */
    return extract32(m34, 4 + 3 - 1, 1);
}

/* 32-bit FP addition */
uint64_t HELPER(aeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_add(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64-bit FP addition */
uint64_t HELPER(adb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_add(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 128-bit FP addition */
Int128 HELPER(axb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_add(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

/* 32-bit FP subtraction */
uint64_t HELPER(seb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_sub(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64-bit FP subtraction */
uint64_t HELPER(sdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_sub(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 128-bit FP subtraction */
Int128 HELPER(sxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_sub(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

/* 32-bit FP division */
uint64_t HELPER(deb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_div(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64-bit FP division */
uint64_t HELPER(ddb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_div(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 128-bit FP division */
Int128 HELPER(dxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_div(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

static float128 float128_precision_round_to_float32(float128 x)
{
    x.low = 0;
    x.high = deposit64(x.high, 0, 25, 0);
    return x;
}

static float128 float128_precision_round_to_float64(float128 x)
{
    x.low = deposit64(x.low, 0, 60, 0);
    return x;
}

static int float128_get_exp(float128 x)
{
    return extract64(x.high, 48, 15) - 16383;
}

static float128 float128_set_exp(float128 x, int exp)
{
    x.high = deposit64(x.high, 48, 15, exp + 16383);
    return x;
}

static float128 float128_adjust_exp(float128 x, int delta)
{
    return float128_set_exp(x, float128_get_exp(x) + delta);
}

static bool float128_is_int(float128 x)
{
    return extract64(x.high, 0, 48) == 0 && x.low == 0;
}

static float32 extract_float32(CPUS390XState *env, uint32_t r)
{
    return env->vregs[r][0] >> 32;
}

static void deposit_float32(CPUS390XState *env, uint32_t r, float32 x)
{
    env->vregs[r][0] = deposit64(env->vregs[r][0], 32, 32, x);
}

static float64 extract_float64(CPUS390XState *env, uint32_t r)
{
    return env->vregs[r][0];
}

static void deposit_float64(CPUS390XState *env, uint32_t r, float64 x)
{
    env->vregs[r][0] = x;
}

#define DIVIDE_TO_INTEGER(name, floatN, p, exp_max, exp_bias)                  \
void HELPER(name)(CPUS390XState *env, uint32_t r1, uint32_t r2,                \
                  uint32_t r3, uint32_t m4)                                    \
{                                                                              \
    int float_exception_flags = 0;                                             \
    floatN a, b, n, r;                                                         \
    int dxc = -1;                                                              \
    uint32_t cc;                                                               \
                                                                               \
    a = extract_ ## floatN(env, r1);                                           \
    b = extract_ ## floatN(env, r2);                                           \
                                                                               \
    /* POp table "Results: DIVIDE TO INTEGER (Part 1 of 2)" */                 \
    if (floatN ## _is_signaling_nan(a, &env->fpu_status)) {                    \
        r = n = floatN ## _silence_nan(a, &env->fpu_status);                   \
        cc = 1;                                                                \
        float_exception_flags |= float_flag_invalid;                           \
    } else if (floatN ## _is_signaling_nan(b, &env->fpu_status)) {             \
        r = n = floatN ## _silence_nan(b, &env->fpu_status);                   \
        cc = 1;                                                                \
        float_exception_flags |= float_flag_invalid;                           \
    } else if (floatN ## _is_quiet_nan(a, &env->fpu_status)) {                 \
        r = n = a;                                                             \
        cc = 1;                                                                \
    } else if (floatN ## _is_quiet_nan(b, &env->fpu_status)) {                 \
        r = n = b;                                                             \
        cc = 1;                                                                \
    } else if (floatN ## _is_infinity(a) || floatN ## _is_zero(b)) {           \
        r = n = floatN ## _default_nan(&env->fpu_status);                      \
        cc = 1;                                                                \
        float_exception_flags |= float_flag_invalid;                           \
    } else if (floatN ## _is_infinity(b))  {                                   \
        r = a;                                                                 \
        n = floatN ## _set_sign(floatN ## _zero,                               \
                                floatN ## _is_neg(a) != floatN ## _is_neg(b)); \
        cc = 0;                                                                \
    } else {                                                                   \
        float128 a128, b128, m128, n128, q128, r128;                           \
        bool is_final, is_q128_smallish;                                       \
        int old_mode, r128_exp;                                                \
        uint32_t r_flags;                                                      \
                                                                               \
        /* Compute precise quotient */                                         \
        a128 = floatN ## _to_float128(a, &env->fpu_status);                    \
        b128 = floatN ## _to_float128(b, &env->fpu_status);                    \
        q128 = float128_div(a128, b128, &env->fpu_status);                     \
                                                                               \
        /* Final or partial case? */                                           \
        is_q128_smallish = float128_get_exp(q128) < p;                         \
        is_final = is_q128_smallish || float128_is_int(q128);                  \
                                                                               \
        /*                                                                     \
         * Final quotient is rounded using M4,                                 \
         * partial quotient is rounded toward zero.                            \
         */                                                                    \
        old_mode = s390_swap_bfp_rounding_mode(env, is_final ? m4 : 5);        \
        n128 = float128_round_to_int(q128, &env->fpu_status);                  \
        s390_restore_bfp_rounding_mode(env, old_mode);                         \
                                                                               \
        /*                                                                     \
         * Intermediate values are precision-rounded,                          \
         * see "Intermediate Values" in POp.                                   \
         */                                                                    \
        n128 = float128_precision_round_to_ ## floatN(n128);                   \
                                                                               \
        /* Compute remainder */                                                \
        m128 = float128_mul(b128, n128, &env->fpu_status);                     \
        env->fpu_status.float_exception_flags = 0;                             \
        r128 = float128_sub(a128, m128, &env->fpu_status);                     \
        r128_exp = float128_get_exp(r128);                                     \
        r = float128_to_## floatN(r128, &env->fpu_status);                     \
        r_flags = env->fpu_status.float_exception_flags;                       \
                                                                               \
        /* POp table "Results: DIVIDE TO INTEGER (Part 2 of 2)" */             \
        if (is_q128_smallish) {                                                \
            cc = 0;                                                            \
            if (!floatN ## _is_zero(r)) {                                      \
                if (r128_exp < -(exp_max - 1)) {                               \
                    if ((env->fpc >> 24) & S390_IEEE_MASK_UNDERFLOW) {         \
                        float_exception_flags |= float_flag_underflow;         \
                        dxc = 0x10;                                            \
                        r128 = float128_adjust_exp(r128, exp_bias);            \
                        r = float128_to_## floatN(r128, &env->fpu_status);     \
                    }                                                          \
                } else if (r_flags & float_flag_inexact) {                     \
                    float_exception_flags |= float_flag_inexact;               \
                    if ((env->fpc >> 24) & S390_IEEE_MASK_INEXACT) {           \
                        /*                                                     \
                         * Check whether remainder was truncated (rounded      \
                         * toward zero) or incremented.                        \
                         */                                                    \
                        if (float128_lt(                                       \
                                floatN ## _to_float128(floatN ## _abs(r),      \
                                                       &env->fpu_status),      \
                                float128_abs(r128), &env->fpu_status)) {       \
                           dxc = 0x8;                                          \
                        } else {                                               \
                           dxc = 0xc;                                          \
                        }                                                      \
                    }                                                          \
                }                                                              \
            }                                                                  \
        } else if (float128_get_exp(n128) > exp_max) {                         \
            n128 = float128_adjust_exp(n128, -exp_bias);                       \
            cc = floatN ## _is_zero(r) ? 1 : 3;                                \
        } else {                                                               \
            cc = floatN ## _is_zero(r) ? 0 : 2;                                \
        }                                                                      \
                                                                               \
        /* Adjust sign of zero */                                              \
        if (floatN ## _is_zero(r)) {                                           \
            r = floatN ## _set_sign(r, float128_is_neg(a128));                 \
        }                                                                      \
        n = float128_to_ ## floatN(n128, &env->fpu_status);                    \
        if (floatN ## _is_zero(n)) {                                           \
            n = floatN ## _set_sign(n,                                         \
                                    float128_is_neg(a128) !=                   \
                                        float128_is_neg(b128));                \
        }                                                                      \
    }                                                                          \
                                                                               \
    /* Flush the results if needed */                                          \
    if ((float_exception_flags & float_flag_invalid) &&                        \
        ((env->fpc >> 24) & S390_IEEE_MASK_INVALID)) {                         \
        /* The action for invalid operation is "Suppress" */                   \
    } else {                                                                   \
        /* The action for other exceptions is "Complete" */                    \
        deposit_ ## floatN(env, r1, r);                                        \
        deposit_ ## floatN(env, r3, n);                                        \
        env->cc_op = cc;                                                       \
    }                                                                          \
                                                                               \
    /* Raise an exception if needed */                                         \
    if (dxc == -1) {                                                           \
        env->fpu_status.float_exception_flags = float_exception_flags;         \
        handle_exceptions(env, false, GETPC());                                \
    } else {                                                                   \
        env->fpu_status.float_exception_flags = 0;                             \
        tcg_s390_data_exception(env, dxc, GETPC());                            \
    }                                                                          \
}

DIVIDE_TO_INTEGER(dieb, float32, 24, 127, 192)
DIVIDE_TO_INTEGER(didb, float64, 53, 1023, 1536)

/* 32-bit FP multiplication */
uint64_t HELPER(meeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float32 ret = float32_mul(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64-bit FP multiplication */
uint64_t HELPER(mdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 ret = float64_mul(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64/32-bit FP multiplication */
uint64_t HELPER(mdeb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float64 f1_64 = float32_to_float64(f1, &env->fpu_status);
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    ret = float64_mul(f1_64, ret, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 128-bit FP multiplication */
Int128 HELPER(mxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    float128 ret = float128_mul(ARG128(a), ARG128(b), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

/* 128/64-bit FP multiplication */
Int128 HELPER(mxdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    float128 f1_128 = float64_to_float128(f1, &env->fpu_status);
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    ret = float128_mul(f1_128, ret, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

/* convert 32-bit float to 64-bit float */
uint64_t HELPER(ldeb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float32_to_float64(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* convert 128-bit float to 64-bit float */
uint64_t HELPER(ldxb)(CPUS390XState *env, Int128 a, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = float128_to_float64(ARG128(a), &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* convert 64-bit float to 128-bit float */
Int128 HELPER(lxdb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float64_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

/* convert 32-bit float to 128-bit float */
Int128 HELPER(lxeb)(CPUS390XState *env, uint64_t f2)
{
    float128 ret = float32_to_float128(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

/* convert 64-bit float to 32-bit float */
uint64_t HELPER(ledb)(CPUS390XState *env, uint64_t f2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = float64_to_float32(f2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* convert 128-bit float to 32-bit float */
uint64_t HELPER(lexb)(CPUS390XState *env, Int128 a, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = float128_to_float32(ARG128(a), &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* 32-bit FP compare */
uint32_t HELPER(ceb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float32_compare_quiet(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 64-bit FP compare */
uint32_t HELPER(cdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float64_compare_quiet(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 128-bit FP compare */
uint32_t HELPER(cxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    FloatRelation cmp = float128_compare_quiet(ARG128(a), ARG128(b),
                                               &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

int s390_swap_bfp_rounding_mode(CPUS390XState *env, int m3)
{
    int ret = env->fpu_status.float_rounding_mode;

    switch (m3) {
    case 0:
        /* current mode */
        break;
    case 1:
        /* round to nearest with ties away from 0 */
        set_float_rounding_mode(float_round_ties_away, &env->fpu_status);
        break;
    case 3:
        /* round to prepare for shorter precision */
        set_float_rounding_mode(float_round_to_odd, &env->fpu_status);
        break;
    case 4:
        /* round to nearest with ties to even */
        set_float_rounding_mode(float_round_nearest_even, &env->fpu_status);
        break;
    case 5:
        /* round to zero */
        set_float_rounding_mode(float_round_to_zero, &env->fpu_status);
        break;
    case 6:
        /* round to +inf */
        set_float_rounding_mode(float_round_up, &env->fpu_status);
        break;
    case 7:
        /* round to -inf */
        set_float_rounding_mode(float_round_down, &env->fpu_status);
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

void s390_restore_bfp_rounding_mode(CPUS390XState *env, int old_mode)
{
    set_float_rounding_mode(old_mode, &env->fpu_status);
}

/* convert 64-bit int to 32-bit float */
uint64_t HELPER(cegb)(CPUS390XState *env, int64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = int64_to_float32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* convert 64-bit int to 64-bit float */
uint64_t HELPER(cdgb)(CPUS390XState *env, int64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = int64_to_float64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* convert 64-bit int to 128-bit float */
Int128 HELPER(cxgb)(CPUS390XState *env, int64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 ret = int64_to_float128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return RET128(ret);
}

/* convert 64-bit uint to 32-bit float */
uint64_t HELPER(celgb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = uint64_to_float32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* convert 64-bit uint to 64-bit float */
uint64_t HELPER(cdlgb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = uint64_to_float64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* convert 64-bit uint to 128-bit float */
Int128 HELPER(cxlgb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 ret = uint64_to_float128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return RET128(ret);
}

/* convert 32-bit float to 64-bit int */
uint64_t HELPER(cgeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int64_t ret = float32_to_int64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return INT64_MIN;
    }
    return ret;
}

/* convert 64-bit float to 64-bit int */
uint64_t HELPER(cgdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int64_t ret = float64_to_int64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return INT64_MIN;
    }
    return ret;
}

/* convert 128-bit float to 64-bit int */
uint64_t HELPER(cgxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    int64_t ret = float128_to_int64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return INT64_MIN;
    }
    return ret;
}

/* convert 32-bit float to 32-bit int */
uint64_t HELPER(cfeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int32_t ret = float32_to_int32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return INT32_MIN;
    }
    return ret;
}

/* convert 64-bit float to 32-bit int */
uint64_t HELPER(cfdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    int32_t ret = float64_to_int32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return INT32_MIN;
    }
    return ret;
}

/* convert 128-bit float to 32-bit int */
uint64_t HELPER(cfxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    int32_t ret = float128_to_int32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return INT32_MIN;
    }
    return ret;
}

/* convert 32-bit float to 64-bit uint */
uint64_t HELPER(clgeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint64_t ret = float32_to_uint64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

/* convert 64-bit float to 64-bit uint */
uint64_t HELPER(clgdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint64_t ret = float64_to_uint64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

/* convert 128-bit float to 64-bit uint */
uint64_t HELPER(clgxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    uint64_t ret = float128_to_uint64(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

/* convert 32-bit float to 32-bit uint */
uint64_t HELPER(clfeb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint32_t ret = float32_to_uint32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f32(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float32_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

/* convert 64-bit float to 32-bit uint */
uint64_t HELPER(clfdb)(CPUS390XState *env, uint64_t v2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    uint32_t ret = float64_to_uint32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f64(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float64_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

/* convert 128-bit float to 32-bit uint */
uint64_t HELPER(clfxb)(CPUS390XState *env, Int128 i2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 v2 = ARG128(i2);
    uint32_t ret = float128_to_uint32(v2, &env->fpu_status);
    uint32_t cc = set_cc_conv_f128(v2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    env->cc_op = cc;
    if (float128_is_any_nan(v2)) {
        return 0;
    }
    return ret;
}

/* round to integer 32-bit */
uint64_t HELPER(fieb)(CPUS390XState *env, uint64_t f2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float32 ret = float32_round_to_int(f2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* round to integer 64-bit */
uint64_t HELPER(fidb)(CPUS390XState *env, uint64_t f2, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float64 ret = float64_round_to_int(f2, &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return ret;
}

/* round to integer 128-bit */
Int128 HELPER(fixb)(CPUS390XState *env, Int128 a, uint32_t m34)
{
    int old_mode = s390_swap_bfp_rounding_mode(env, round_from_m34(m34));
    float128 ret = float128_round_to_int(ARG128(a), &env->fpu_status);

    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_exceptions(env, xxc_from_m34(m34), GETPC());
    return RET128(ret);
}

/* 32-bit FP compare and signal */
uint32_t HELPER(keb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float32_compare(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 64-bit FP compare and signal */
uint32_t HELPER(kdb)(CPUS390XState *env, uint64_t f1, uint64_t f2)
{
    FloatRelation cmp = float64_compare(f1, f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 128-bit FP compare and signal */
uint32_t HELPER(kxb)(CPUS390XState *env, Int128 a, Int128 b)
{
    FloatRelation cmp = float128_compare(ARG128(a), ARG128(b),
                                         &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return float_comp_to_cc(env, cmp);
}

/* 32-bit FP multiply and add */
uint64_t HELPER(maeb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float32 ret = float32_muladd(f3, f2, f1, 0, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64-bit FP multiply and add */
uint64_t HELPER(madb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float64 ret = float64_muladd(f3, f2, f1, 0, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 32-bit FP multiply and subtract */
uint64_t HELPER(mseb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float32 ret = float32_muladd(f3, f2, f1, float_muladd_negate_c,
                                 &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* 64-bit FP multiply and subtract */
uint64_t HELPER(msdb)(CPUS390XState *env, uint64_t f1,
                      uint64_t f2, uint64_t f3)
{
    float64 ret = float64_muladd(f3, f2, f1, float_muladd_negate_c,
                                 &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* The rightmost bit has the number 11. */
static inline uint16_t dcmask(int bit, bool neg)
{
    return 1 << (11 - bit - neg);
}

#define DEF_FLOAT_DCMASK(_TYPE) \
uint16_t _TYPE##_dcmask(CPUS390XState *env, _TYPE f1)              \
{                                                                  \
    const bool neg = _TYPE##_is_neg(f1);                           \
                                                                   \
    /* Sorted by most common cases - only one class is possible */ \
    if (_TYPE##_is_normal(f1)) {                                   \
        return dcmask(2, neg);                                     \
    } else if (_TYPE##_is_zero(f1)) {                              \
        return dcmask(0, neg);                                     \
    } else if (_TYPE##_is_denormal(f1)) {                          \
        return dcmask(4, neg);                                     \
    } else if (_TYPE##_is_infinity(f1)) {                          \
        return dcmask(6, neg);                                     \
    } else if (_TYPE##_is_quiet_nan(f1, &env->fpu_status)) {       \
        return dcmask(8, neg);                                     \
    }                                                              \
    /* signaling nan, as last remaining case */                    \
    return dcmask(10, neg);                                        \
}
DEF_FLOAT_DCMASK(float32)
DEF_FLOAT_DCMASK(float64)
DEF_FLOAT_DCMASK(float128)

/* test data class 32-bit */
uint32_t HELPER(tceb)(CPUS390XState *env, uint64_t f1, uint64_t m2)
{
    return (m2 & float32_dcmask(env, f1)) != 0;
}

/* test data class 64-bit */
uint32_t HELPER(tcdb)(CPUS390XState *env, uint64_t v1, uint64_t m2)
{
    return (m2 & float64_dcmask(env, v1)) != 0;
}

/* test data class 128-bit */
uint32_t HELPER(tcxb)(CPUS390XState *env, Int128 a, uint64_t m2)
{
    return (m2 & float128_dcmask(env, ARG128(a))) != 0;
}

/* square root 32-bit */
uint64_t HELPER(sqeb)(CPUS390XState *env, uint64_t f2)
{
    float32 ret = float32_sqrt(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* square root 64-bit */
uint64_t HELPER(sqdb)(CPUS390XState *env, uint64_t f2)
{
    float64 ret = float64_sqrt(f2, &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return ret;
}

/* square root 128-bit */
Int128 HELPER(sqxb)(CPUS390XState *env, Int128 a)
{
    float128 ret = float128_sqrt(ARG128(a), &env->fpu_status);
    handle_exceptions(env, false, GETPC());
    return RET128(ret);
}

static const int fpc_to_rnd[8] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down,
    -1,
    -1,
    -1,
    float_round_to_odd,
};

/* set fpc */
void HELPER(sfpc)(CPUS390XState *env, uint64_t fpc)
{
    if (fpc_to_rnd[fpc & 0x7] == -1 || fpc & 0x03030088u ||
        (!s390_has_feat(S390_FEAT_FLOATING_POINT_EXT) && fpc & 0x4)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }

    /* Install everything in the main FPC.  */
    env->fpc = fpc;

    /* Install the rounding mode in the shadow fpu_status.  */
    set_float_rounding_mode(fpc_to_rnd[fpc & 0x7], &env->fpu_status);
}

/* set fpc and signal */
void HELPER(sfas)(CPUS390XState *env, uint64_t fpc)
{
    uint32_t signalling = env->fpc;
    uint32_t s390_exc;

    if (fpc_to_rnd[fpc & 0x7] == -1 || fpc & 0x03030088u ||
        (!s390_has_feat(S390_FEAT_FLOATING_POINT_EXT) && fpc & 0x4)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }

    /*
     * FPC is set to the FPC operand with a bitwise OR of the signalling
     * flags.
     */
    env->fpc = fpc | (signalling & 0x00ff0000);
    set_float_rounding_mode(fpc_to_rnd[fpc & 0x7], &env->fpu_status);

    /*
     * If any signaling flag is enabled in the new FPC mask, a
     * simulated-iee-exception exception occurs.
     */
    s390_exc = (signalling >> 16) & (fpc >> 24);
    if (s390_exc) {
        if (s390_exc & S390_IEEE_MASK_INVALID) {
            s390_exc = S390_IEEE_MASK_INVALID;
        } else if (s390_exc & S390_IEEE_MASK_DIVBYZERO) {
            s390_exc = S390_IEEE_MASK_DIVBYZERO;
        } else if (s390_exc & S390_IEEE_MASK_OVERFLOW) {
            s390_exc &= (S390_IEEE_MASK_OVERFLOW | S390_IEEE_MASK_INEXACT);
        } else if (s390_exc & S390_IEEE_MASK_UNDERFLOW) {
            s390_exc &= (S390_IEEE_MASK_UNDERFLOW | S390_IEEE_MASK_INEXACT);
        } else if (s390_exc & S390_IEEE_MASK_INEXACT) {
            s390_exc = S390_IEEE_MASK_INEXACT;
        } else if (s390_exc & S390_IEEE_MASK_QUANTUM) {
            s390_exc = S390_IEEE_MASK_QUANTUM;
        }
        tcg_s390_data_exception(env, s390_exc | 3, GETPC());
    }
}

/* set bfp rounding mode */
void HELPER(srnm)(CPUS390XState *env, uint64_t rnd)
{
    if (rnd > 0x7 || fpc_to_rnd[rnd & 0x7] == -1) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }

    env->fpc = deposit32(env->fpc, 0, 3, rnd);
    set_float_rounding_mode(fpc_to_rnd[rnd & 0x7], &env->fpu_status);
}
