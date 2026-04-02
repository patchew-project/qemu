/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hvx_ieee_fp.h"

#define DEF_FP_INSN_2(name, rt, a1t, a2t, op) \
    uint##rt##_t fp_##name(uint##a1t##_t a1, uint##a2t##_t a2, \
                           float_status *fp_status) { \
        float##a1t f1 = make_float##a1t(a1); \
        float##a2t f2 = make_float##a2t(a2); \
        return (op); \
    }

#define DEF_FP_INSN_3(name, rt, a1t, a2t, a3t, op) \
    uint##rt##_t fp_##name(uint##a1t##_t a1, uint##a2t##_t a2, \
                           uint##a3t##_t a3, float_status *fp_status) { \
        float##a1t f1 = make_float##a1t(a1); \
        float##a2t f2 = make_float##a2t(a2); \
        float##a3t f3 = make_float##a3t(a3); \
        return (op); \
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
    if (float32_is_pos_nan(f1) || float32_is_neg_nan(f2)) {
        return a1;
    }
    if (float32_is_pos_nan(f2) || float32_is_neg_nan(f1)) {
        return a2;
    }
    return fp_max_sf(a1, a2, fp_status);
}

uint32_t qf_min_sf(uint32_t a1, uint32_t a2, float_status *fp_status)
{
    float32 f1 = make_float32(a1);
    float32 f2 = make_float32(a2);
    if (float32_is_pos_nan(f1) || float32_is_neg_nan(f2)) {
        return a2;
    }
    if (float32_is_pos_nan(f2) || float32_is_neg_nan(f1)) {
        return a1;
    }
    return fp_min_sf(a1, a2, fp_status);
}

uint16_t qf_max_hf(uint16_t a1, uint16_t a2, float_status *fp_status)
{
    float16 f1 = make_float16(a1);
    float16 f2 = make_float16(a2);
    if (float16_is_pos_nan(f1) || float16_is_neg_nan(f2)) {
        return a1;
    }
    if (float16_is_pos_nan(f2) || float16_is_neg_nan(f1)) {
        return a2;
    }
    return fp_max_hf(a1, a2, fp_status);
}

uint16_t qf_min_hf(uint16_t a1, uint16_t a2, float_status *fp_status)
{
    float16 f1 = make_float16(a1);
    float16 f2 = make_float16(a2);
    if (float16_is_pos_nan(f1) || float16_is_neg_nan(f2)) {
        return a2;
    }
    if (float16_is_pos_nan(f2) || float16_is_neg_nan(f1)) {
        return a1;
    }
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
    float32 f1 = make_float32(a);
    /* float32_to_int32 converts any NaN to MAX, hexagon looks at the sign. */
    if (float32_is_any_nan(f1)) {
        return float32_is_neg(f1) ? INT32_MIN : INT32_MAX;
    }
    return float32_to_int32_round_to_zero(f1, fp_status);
}

int16_t conv_h_hf(uint16_t a, float_status *fp_status)
{
    float16 f1 = make_float16(a);
    /* float16_to_int16 converts any NaN to MAX, hexagon looks at the sign. */
    if (float16_is_any_nan(f1)) {
        return float16_is_neg(f1) ? INT16_MIN : INT16_MAX;
    }
    return float16_to_int16_round_to_zero(f1, fp_status);
}

/*
 * Returns true if f1 > f2, where at least one of the elements is guaranteed
 * to be NaN.
 * Up to v73, Hexagon HVX IEEE FP follows this order:
 * QNaN > SNaN > +Inf > numbers > -Inf > SNaN_neg > QNaN_neg
 */
static bool float32_nan_compare(float32 f1, float32 f2, float_status *fp_status)
{
    /* opposite signs case */
    if (float32_is_neg(f1) != float32_is_neg(f2)) {
        return !float32_is_neg(f1);
    }

    /* same sign case */
    bool result = (float32_is_any_nan(f1) && !float32_is_any_nan(f2)) ||
        (float32_is_quiet_nan(f1, fp_status) && !float32_is_quiet_nan(f2, fp_status));
    return float32_is_neg(f1) ? !result : result;
}

static bool float16_nan_compare(float16 f1, float16 f2, float_status *fp_status)
{
    /* opposite signs case */
    if (float16_is_neg(f1) != float16_is_neg(f2)) {
        return !float16_is_neg(f1);
    }

    /* same sign case */
    bool result = (float16_is_any_nan(f1) && !float16_is_any_nan(f2)) ||
        (float16_is_quiet_nan(f1, fp_status) && !float16_is_quiet_nan(f2, fp_status));
    return float16_is_neg(f1) ? !result : result;
}

uint32_t cmpgt_sf(uint32_t a1, uint32_t a2, float_status *fp_status)
{
    float32 f1 = make_float32(a1);
    float32 f2 = make_float32(a2);
    if (float32_is_any_nan(f1) || float32_is_any_nan(f2)) {
        return float32_nan_compare(f1, f2, fp_status);
    }
    return float32_compare(a1, a2, fp_status) == float_relation_greater;
}

uint16_t cmpgt_hf(uint16_t a1, uint16_t a2, float_status *fp_status)
{
    float16 f1 = make_float16(a1);
    float16 f2 = make_float16(a2);
    if (float16_is_any_nan(f1) || float16_is_any_nan(f2)) {
        return float16_nan_compare(f1, f2, fp_status);
    }
    return float16_compare(a1, a2, fp_status) == float_relation_greater;
}

DEF_FP_INSN_3(mult_sf_bf_acc, 32, 16, 16, 32,
              float32_muladd(bf_to_sf(f1, fp_status), bf_to_sf(f2, fp_status),
                             f3, 0, fp_status))
