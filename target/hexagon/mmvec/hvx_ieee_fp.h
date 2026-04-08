/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HVX_IEEE_H
#define HEXAGON_HVX_IEEE_H

#include "fpu/softfloat.h"

#define f16_to_f32(A) float16_to_float32((A), true, &env->hvx_fp_status)
#define f32_to_f16(A) float32_to_float16((A), true, &env->hvx_fp_status)

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

#endif
