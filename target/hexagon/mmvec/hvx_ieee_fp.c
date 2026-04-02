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
