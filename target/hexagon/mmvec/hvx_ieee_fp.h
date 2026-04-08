/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HVX_IEEE_H
#define HEXAGON_HVX_IEEE_H

#include "fpu/softfloat.h"

#define FP32_DEF_NAN 0x7FFFFFFF

#define f16_to_f32(A) float16_to_float32((A), true, &env->hvx_fp_status)
#define f32_to_f16(A) float32_to_float16((A), true, &env->hvx_fp_status)
#define bf_to_sf(A) bfloat16_to_float32(A, &env->hvx_fp_status)

float32 fp_mult_sf_hf(float16 a1, float16 a2, float_status *fp_status);
float32 fp_vdmpy(float16 a1, float16 a2, float16 a3, float16 a4,
                 float_status *fp_status);

/* Qfloat min/max treat +NaN as greater than +INF and -NaN as smaller than -INF */
uint32_t qf_max_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint32_t qf_min_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint16_t qf_max_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint16_t qf_min_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

int32_t conv_w_sf(float32 a, float_status *fp_status);
int16_t conv_h_hf(float16 a, float_status *fp_status);

/* IEEE - FP compare instructions */
uint32_t cmpgt_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint16_t cmpgt_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

/* IEEE BFloat instructions */

#define fp_mult_sf_bf(A, B) \
    float32_mul(bf_to_sf(A), bf_to_sf(B), &env->hvx_fp_status)

#define fp_add_sf_bf(A, B) \
    float32_add(bf_to_sf(A), bf_to_sf(B), &env->hvx_fp_status)

#define fp_sub_sf_bf(A, B) \
    float32_sub(bf_to_sf(A), bf_to_sf(B), &env->hvx_fp_status)

#define fp_mult_sf_bf_acc(f1, f2, f3) \
    float32_muladd(bf_to_sf(f1), bf_to_sf(f2), f3, 0, &env->hvx_fp_status)

static inline uint16_t sf_to_bf(int32_t A, float_status *fp_status)
{
    uint32_t rslt = A;
    if ((rslt & 0x1FFFF) == 0x08000) {
        /* do not round up if exactly .5 and even already */
    } else if ((rslt & 0x8000) == 0x8000) {
        rslt += 0x8000; /* rounding to nearest number */
    }
    rslt = float32_is_any_nan(A) ? FP32_DEF_NAN : rslt;
    return float32_to_bfloat16(rslt, fp_status);
}

#define fp_min_bf(A, B) \
    sf_to_bf(float32_min(bf_to_sf(A), bf_to_sf(B), &env->hvx_fp_status), \
             &env->hvx_fp_status);

#define fp_max_bf(A, B) \
    sf_to_bf(float32_max(bf_to_sf(A), bf_to_sf(B), &env->hvx_fp_status), \
             &env->hvx_fp_status);

#endif
