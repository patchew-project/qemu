/*
 * RISC-V Bulk Normalization Algorithm (RVBNA)
 *
 * Copyright (c) 2026 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"
#include "fpu/softfloat.h"
#include "fpu/softfloat-macros.h"
#include "rvbna.h"

static inline int rvbna_guard_bits(uint32_t n_prod)
{
    g_assert(n_prod > 0);
    return 32 - clz32(n_prod - 1);
}

float32 rvbna_final_f32_add(float32 partial, float32 accumulator,
                            float_status *fp_status)
{
    FloatRoundMode old_rm = get_float_rounding_mode(fp_status);
    int old_flags = get_float_exception_flags(fp_status);
    int new_flags;
    float32 result;

    set_float_rounding_mode(float_round_to_odd_inf, fp_status);
    set_float_exception_flags(0, fp_status);
    result = float32_add(partial, accumulator, fp_status);
    new_flags = get_float_exception_flags(fp_status) &
                (float_flag_invalid | float_flag_overflow);
    set_float_rounding_mode(old_rm, fp_status);
    set_float_exception_flags(old_flags | new_flags, fp_status);

    return result;
}

#define RVBNA_STACK_THRESHOLD 8

#define GEN_RVBNA(TA, TB, TO, A_E, A_M, B_E, B_M, OUT_E, OUT_M)              \
TO rvbna_dot_##TA##_##TB(const TA a[], const TB b[], uint32_t n_prod,        \
                         float_status *fp_status)                            \
{                                                                            \
    int32_t stack_ref_exps[RVBNA_STACK_THRESHOLD];                           \
    bool stack_signs[RVBNA_STACK_THRESHOLD];                                 \
    uint64_t stack_sigs[RVBNA_STACK_THRESHOLD];                              \
    int32_t *prod_ref_exps = stack_ref_exps;                                 \
    bool *prod_signs = stack_signs;                                          \
    uint64_t *prod_sigs = stack_sigs;                                        \
    g_autofree int32_t *heap_ref_exps = NULL;                                \
    g_autofree bool *heap_signs = NULL;                                      \
    g_autofree uint64_t *heap_sigs = NULL;                                   \
    int guard_bits = rvbna_guard_bits(n_prod);                               \
    int pad = OUT_M - (A_M + B_M) + guard_bits;                              \
    int output_bias = (1 << (OUT_E - 1)) - 1;                                \
    int a_exp_bias = (1 << (A_E - 1)) - 1;                                   \
    int b_exp_bias = (1 << (B_E - 1)) - 1;                                   \
    int ref_bias = output_bias - (a_exp_bias + b_exp_bias);                  \
    int max_prod_ref_exp = ref_bias;                                         \
    int overflow_exp = (1 << OUT_E) - 1;                                     \
    int64_t acc = 0;                                                         \
    bool nan_result = false;                                                 \
    bool inf_result = false;                                                 \
    bool inf_sign = false;                                                   \
    bool any_pos_inf = false;                                                \
    bool any_neg_inf = false;                                                \
    bool any_invalid_prod = false;                                           \
    bool any_signan = false;                                                 \
    bool op_sign_inf, sign, overflow;                                        \
    uint32_t i;                                                              \
    uint64_t sig;                                                            \
    int lead, exp, sig_bits;                                                 \
                                                                             \
    g_assert(pad >= 0);                                                      \
    if (n_prod > RVBNA_STACK_THRESHOLD) {                                    \
        heap_ref_exps = g_new(int32_t, n_prod);                              \
        heap_signs = g_new(bool, n_prod);                                    \
        heap_sigs = g_new(uint64_t, n_prod);                                 \
        prod_ref_exps = heap_ref_exps;                                       \
        prod_signs = heap_signs;                                             \
        prod_sigs = heap_sigs;                                               \
    }                                                                        \
                                                                             \
    for (i = 0; i < n_prod; i++) {                                           \
        bool a_sign = TA##_is_neg(a[i]);                                     \
        bool b_sign = TB##_is_neg(b[i]);                                     \
        bool a_sub = TA##_is_zero_or_denormal(a[i]);                         \
        bool b_sub = TB##_is_zero_or_denormal(b[i]);                         \
        bool a_zero = TA##_is_zero(a[i]);                                    \
        bool b_zero = TB##_is_zero(b[i]);                                    \
        bool a_inf = TA##_is_infinity(a[i]);                                 \
        bool b_inf = TB##_is_infinity(b[i]);                                 \
        bool a_nan = TA##_is_any_nan(a[i]);                                  \
        bool b_nan = TB##_is_any_nan(b[i]);                                  \
        bool prod_sign = a_sign ^ b_sign;                                    \
        bool invalid_prod = (a_inf && b_zero) || (b_inf && a_zero);          \
        bool inf_prod_lhs = a_inf && !b_nan && !b_zero;                      \
        bool inf_prod_rhs = b_inf && !a_nan && !a_zero;                      \
        bool inf_prod = inf_prod_lhs || inf_prod_rhs;                        \
        bool invalid_sum = inf_result && inf_prod &&                         \
                           (inf_sign != prod_sign);                          \
        uint32_t a_mant = extract32(a[i], 0, A_M);                           \
        uint32_t b_mant = extract32(b[i], 0, B_M);                           \
        bool a_signan = a_nan && TA##_is_signaling_nan(a[i], fp_status);     \
        bool b_signan = b_nan && TB##_is_signaling_nan(b[i], fp_status);     \
        uint64_t a_sig = a_mant | (!a_sub << A_M);                           \
        uint64_t b_sig = b_mant | (!b_sub << B_M);                           \
        bool prod_is_zero = a_zero || b_zero;                                \
        bool either_inf = a_inf || b_inf;                                    \
        bool either_nan = a_nan || b_nan;                                    \
                                                                             \
        any_invalid_prod |= invalid_prod;                                    \
        nan_result |= a_nan || b_nan || any_invalid_prod || invalid_sum;     \
        inf_result |= inf_prod;                                              \
        inf_sign = inf_prod ? prod_sign : inf_sign;                          \
                                                                             \
        prod_signs[i] = prod_sign;                                           \
        prod_sigs[i] = (uint64_t)(a_sig * b_sig) << pad;                     \
        prod_ref_exps[i] = ref_bias;                                         \
        if (!prod_is_zero) {                                                 \
            prod_ref_exps[i] += (a_sub ? 1 : extract32(a[i], A_M, A_E)) +    \
                                (b_sub ? 1 : extract32(b[i], B_M, B_E));     \
        }                                                                    \
        max_prod_ref_exp = MAX(prod_ref_exps[i], max_prod_ref_exp);          \
                                                                             \
        any_pos_inf |= (either_inf && !either_nan && !prod_is_zero &&        \
                        (a_sign == b_sign));                                 \
        any_neg_inf |= (either_inf && !either_nan && !prod_is_zero &&        \
                        (a_sign != b_sign));                                 \
        any_signan |= a_signan || b_signan;                                  \
    }                                                                        \
                                                                             \
    op_sign_inf = any_pos_inf && any_neg_inf;                                \
                                                                             \
    if (nan_result) {                                                        \
        if (any_signan || any_invalid_prod || op_sign_inf) {                 \
            float_raise(float_flag_invalid, fp_status);                      \
        }                                                                    \
        return TO##_default_nan(fp_status);                                  \
    } else if (inf_result) {                                                 \
        return TO##_set_sign(TO##_infinity, inf_sign);                       \
    }                                                                        \
                                                                             \
    for (i = 0; i < n_prod; i++) {                                           \
        shift64RightJamming(prod_sigs[i],                                    \
                            max_prod_ref_exp - prod_ref_exps[i], &sig);      \
        acc += prod_signs[i] ? -(int64_t)sig : (int64_t)sig;                 \
    }                                                                        \
                                                                             \
    sign = acc < 0;                                                          \
    sig = sign ? -(uint64_t)acc : acc;                                       \
    if (sig == 0) {                                                          \
        return 0;                                                            \
    }                                                                        \
                                                                             \
    lead = 63 - clz64(sig);                                                  \
    exp = max_prod_ref_exp - OUT_M - guard_bits + lead;                      \
    overflow = exp >= overflow_exp;                                          \
    if (overflow) {                                                          \
        float_raise(float_flag_overflow, fp_status);                         \
        sig = 0;                                                             \
        exp = overflow_exp;                                                  \
    } else {                                                                 \
        sig_bits = MAX(0, exp <= 0 ? OUT_M - (1 - exp) : OUT_M);             \
        if (lead > sig_bits) {                                               \
            shift64RightJamming(sig, lead - sig_bits, &sig);                 \
        } else {                                                             \
            sig <<= (sig_bits - lead);                                       \
        }                                                                    \
        exp = MAX(exp, 0);                                                   \
    }                                                                        \
    return (TO)deposit64(deposit64(sig, OUT_M, OUT_E, exp),                  \
                         OUT_M + OUT_E,                                      \
                         1, sign);                                           \
}

GEN_RVBNA(float8_e4m3, float8_e4m3, float32, 4, 3, 4, 3, 8, 23)
GEN_RVBNA(float8_e4m3, float8_e5m2, float32, 4, 3, 5, 2, 8, 23)
GEN_RVBNA(float8_e5m2, float8_e4m3, float32, 5, 2, 4, 3, 8, 23)
GEN_RVBNA(float8_e5m2, float8_e5m2, float32, 5, 2, 5, 2, 8, 23)
GEN_RVBNA(float16, float16, float32, 5, 10, 5, 10, 8, 23)
GEN_RVBNA(bfloat16, bfloat16, float32, 8, 7, 8, 7, 8, 23)
