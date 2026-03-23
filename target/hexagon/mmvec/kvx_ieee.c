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
