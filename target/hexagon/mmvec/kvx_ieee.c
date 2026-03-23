/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "kvx_ieee.h"

#define DEF_FP_INSN_2(name, rt, a1t, a2t, op) \
    uint##rt##_t fp_##name(uint##a1t##_t a1, uint##a2t##_t a2, \
                           float_status *fp_status) { \
        float##a1t f1 = make_float##a1t(a1); \
        float##a2t f2 = make_float##a2t(a2); \
        \
        if (float##a1t##_is_any_nan(f1) || float##a2t##_is_any_nan(f2)) { \
            return FP##rt##_DEF_NAN; \
        } \
        float##rt result = op; \
        \
        if (float##rt##_is_any_nan(result)) { \
            return FP##rt##_DEF_NAN; \
        } \
        return result; \
    }

#define DEF_FP_INSN_3(name, rt, a1t, a2t, a3t, op) \
    uint##rt##_t fp_##name(uint##a1t##_t a1, uint##a2t##_t a2, \
                           uint##a3t##_t a3, float_status *fp_status) { \
        float##a1t f1 = make_float##a1t(a1); \
        float##a2t f2 = make_float##a2t(a2); \
        float##a3t f3 = make_float##a3t(a3); \
        \
        if (float##a1t##_is_any_nan(f1) || float##a2t##_is_any_nan(f2) || \
            float##a3t##_is_any_nan(f3)) \
            return FP##rt##_DEF_NAN; \
        \
        float##rt result = op; \
        \
        if (float##rt##_is_any_nan(result)) \
            return FP##rt##_DEF_NAN; \
        return result; \
    }

DEF_FP_INSN_2(mult_sf_sf, 32, 32, 32, float32_mul(f1, f2, fp_status))
DEF_FP_INSN_2(add_sf_sf, 32, 32, 32, float32_add(f1, f2, fp_status))
DEF_FP_INSN_2(sub_sf_sf, 32, 32, 32, float32_sub(f1, f2, fp_status))

DEF_FP_INSN_2(mult_hf_hf, 16, 16, 16, float16_mul(f1, f2, fp_status))
DEF_FP_INSN_2(add_hf_hf, 16, 16, 16, float16_add(f1, f2, fp_status))
DEF_FP_INSN_2(sub_hf_hf, 16, 16, 16, float16_sub(f1, f2, fp_status))

DEF_FP_INSN_2(mult_sf_hf, 32, 16, 16,
              float32_mul(float16_to_float32(f1, true, fp_status),
                          float16_to_float32(f2, true, fp_status),
                          fp_status))
DEF_FP_INSN_2(add_sf_hf, 32, 16, 16,
              float32_add(float16_to_float32(f1, true, fp_status),
                          float16_to_float32(f2, true, fp_status),
                          fp_status))
DEF_FP_INSN_2(sub_sf_hf, 32, 16, 16,
              float32_sub(float16_to_float32(f1, true, fp_status),
                          float16_to_float32(f2, true, fp_status),
                          fp_status))

DEF_FP_INSN_3(mult_hf_hf_acc, 16, 16, 16, 16,
              float16_muladd(f1, f2, f3, 0, fp_status))
DEF_FP_INSN_3(mult_sf_hf_acc, 32, 16, 16, 32,
              float32_muladd(float16_to_float32(f1, true, fp_status),
                             float16_to_float32(f2, true, fp_status),
                             f3, 0, fp_status))

uint32_t fp_vdmpy(uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4,
                 float_status *fp_status)
{
    float32 prod1 = fp_mult_sf_hf(a1, a3, fp_status);
    float32 prod2 = fp_mult_sf_hf(a2, a4, fp_status);
    return fp_add_sf_sf(float32_val(prod1), float32_val(prod2), fp_status);
}

uint32_t fp_vdmpy_acc(uint32_t acc, uint16_t a1, uint16_t a2,
                      uint16_t a3, uint16_t a4,
                      float_status *fp_status)
{
    float32 red = fp_vdmpy(a1, a2, a3, a4, fp_status);
    return fp_add_sf_sf(float32_val(red), acc, fp_status);
}

DEF_FP_INSN_2(min_sf, 32, 32, 32, float32_min(f1, f2, fp_status))
DEF_FP_INSN_2(max_sf, 32, 32, 32, float32_max(f1, f2, fp_status))
DEF_FP_INSN_2(min_hf, 16, 16, 16, float16_min(f1, f2, fp_status))
DEF_FP_INSN_2(max_hf, 16, 16, 16, float16_max(f1, f2, fp_status))

#define float32_is_pos_nan(X) (float32_is_any_nan(X) && !float32_is_neg(X))
#define float32_is_neg_nan(X) (float32_is_any_nan(X) && float32_is_neg(X))
#define float16_is_pos_nan(X) (float16_is_any_nan(X) && !float16_is_neg(X))
#define float16_is_neg_nan(X) (float16_is_any_nan(X) && float16_is_neg(X))

uint32_t qf_max_sf(uint32_t a1, uint32_t a2, float_status *fp_status)
{
    float32 f1 = make_float32(a1);
    float32 f2 = make_float32(a2);
    if (float32_is_pos_nan(f1) || float32_is_neg_nan(f2)) return a1;
    if (float32_is_pos_nan(f2) || float32_is_neg_nan(f1)) return a2;
    return fp_max_sf(a1, a2, fp_status);
}

uint32_t qf_min_sf(uint32_t a1, uint32_t a2, float_status *fp_status)
{
    float32 f1 = make_float32(a1);
    float32 f2 = make_float32(a2);
    if (float32_is_pos_nan(f1) || float32_is_neg_nan(f2)) return a2;
    if (float32_is_pos_nan(f2) || float32_is_neg_nan(f1)) return a1;
    return fp_min_sf(a1, a2, fp_status);
}

uint16_t qf_max_hf(uint16_t a1, uint16_t a2, float_status *fp_status)
{
    float16 f1 = make_float16(a1);
    float16 f2 = make_float16(a2);
    if (float16_is_pos_nan(f1) || float16_is_neg_nan(f2)) return a1;
    if (float16_is_pos_nan(f2) || float16_is_neg_nan(f1)) return a2;
    return fp_max_hf(a1, a2, fp_status);
}

uint16_t qf_min_hf(uint16_t a1, uint16_t a2, float_status *fp_status)
{
    float16 f1 = make_float16(a1);
    float16 f2 = make_float16(a2);
    if (float16_is_pos_nan(f1) || float16_is_neg_nan(f2)) return a2;
    if (float16_is_pos_nan(f2) || float16_is_neg_nan(f1)) return a1;
    return fp_min_hf(a1, a2, fp_status);
}

uint16_t f32_to_f16(uint32_t a, float_status *fp_status)
{
    return float16_val(float32_to_float16(make_float32(a), true, fp_status));
}

uint32_t f16_to_f32(uint16_t a, float_status *fp_status)
{
    return float32_val(float16_to_float32(make_float16(a), true, fp_status));
}

uint16_t f16_to_uh(uint16_t op1, float_status *fp_status)
{
    return float16_to_uint16_scalbn(make_float16(op1),
                                    float_round_nearest_even,
                                    0, fp_status);
}

int16_t f16_to_h(uint16_t op1, float_status *fp_status)
{
    return float16_to_int16_scalbn(make_float16(op1),
                                   float_round_nearest_even,
                                   0, fp_status);
}

uint8_t f16_to_ub(uint16_t op1, float_status *fp_status)
{
    return float16_to_uint8_scalbn(make_float16(op1),
                                   float_round_nearest_even,
                                   0, fp_status);
}

int8_t f16_to_b(uint16_t op1, float_status *fp_status)
{
    return float16_to_int8_scalbn(make_float16(op1),
                                   float_round_nearest_even,
                                   0, fp_status);
}

uint16_t uh_to_f16(uint16_t op1)
{
    return uint64_to_float16_scalbn(op1, float_round_nearest_even, 0);
}

uint16_t h_to_f16(int16_t op1)
{
    return int64_to_float16_scalbn(op1, float_round_nearest_even, 0);
}

uint16_t ub_to_f16(uint8_t op1)
{
    return uint64_to_float16_scalbn(op1, float_round_nearest_even, 0);
}

uint16_t b_to_f16(int8_t op1)
{
    return int64_to_float16_scalbn(op1, float_round_nearest_even, 0);
}

int32_t conv_sf_w(int32_t a, float_status *fp_status)
{
    return float32_val(int32_to_float32(a, fp_status));
}

int16_t conv_hf_h(int16_t a, float_status *fp_status)
{
    return float16_val(int16_to_float16(a, fp_status));
}

int32_t conv_w_sf(uint32_t a, float_status *fp_status)
{
    float_status scratch_fpst = {};
    const float32 W_MAX = int32_to_float32(INT32_MAX, &scratch_fpst);
    const float32 W_MIN = int32_to_float32(INT32_MIN, &scratch_fpst);
    float32 f1 = make_float32(a);

    if (float32_is_any_nan(f1) || float32_is_infinity(f1) ||
        float32_le_quiet(W_MAX, f1, fp_status) ||
        float32_le_quiet(f1, W_MIN, fp_status)) {
        return float32_is_neg(f1) ? INT32_MIN : INT32_MAX;
    }
    return float32_to_int32_round_to_zero(f1, fp_status);
}

int16_t conv_h_hf(uint16_t a, float_status *fp_status)
{
    float_status scratch_fpst = {};
    const float16 H_MAX = int16_to_float16(INT16_MAX, &scratch_fpst);
    const float16 H_MIN = int16_to_float16(INT16_MIN, &scratch_fpst);
    float16 f1 = make_float16(a);

    if (float16_is_any_nan(f1) || float16_is_infinity(f1) ||
        float16_le_quiet(H_MAX, f1, fp_status) ||
        float16_le_quiet(f1, H_MIN, fp_status)) {
        return float16_is_neg(f1) ? INT16_MIN : INT16_MAX;
    }
    return float16_to_int16_round_to_zero(f1, fp_status);
}
