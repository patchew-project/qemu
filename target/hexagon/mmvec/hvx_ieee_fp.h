/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HVX_IEEE_H
#define HEXAGON_HVX_IEEE_H

#include "fpu/softfloat.h"

/* Hexagon canonical NaN */
#define FP32_DEF_NAN      0x7FFFFFFF
#define FP16_DEF_NAN      0x7FFF

/*
 * IEEE - FP ADD/SUB/MPY instructions
 */
uint32_t fp_mult_sf_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint32_t fp_add_sf_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint32_t fp_sub_sf_sf(uint32_t a1, uint32_t a2, float_status *fp_status);

uint16_t fp_mult_hf_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint16_t fp_add_hf_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint16_t fp_sub_hf_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

uint32_t fp_mult_sf_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint32_t fp_add_sf_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint32_t fp_sub_sf_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

/*
 * IEEE - FP Accumulate instructions
 */
uint16_t fp_mult_hf_hf_acc(uint16_t a1, uint16_t a2, uint16_t acc,
                           float_status *fp_status);
uint32_t fp_mult_sf_hf_acc(uint16_t a1, uint16_t a2, uint32_t acc,
                           float_status *fp_status);

/*
 * IEEE - FP Reduce instructions
 */
uint32_t fp_vdmpy(uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4,
                  float_status *fp_status);
uint32_t fp_vdmpy_acc(uint32_t acc, uint16_t a1, uint16_t a2, uint16_t a3,
                      uint16_t a4, float_status *fp_status);

/* IEEE - FP min/max instructions */
uint32_t fp_min_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint32_t fp_max_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint16_t fp_min_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint16_t fp_max_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

/* Qfloat min/max treat +NaN as greater than +INF and -NaN as smaller than -INF */
uint32_t qf_max_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint32_t qf_min_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint16_t qf_max_hf(uint16_t a1, uint16_t a2, float_status *fp_status);
uint16_t qf_min_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

/* IEEE - FP compare instructions */
uint32_t cmpgt_sf(uint32_t a1, uint32_t a2, float_status *fp_status);
uint16_t cmpgt_hf(uint16_t a1, uint16_t a2, float_status *fp_status);

/*
 * IEEE - FP Convert instructions
 */
uint16_t f32_to_f16(uint32_t a, float_status *fp_status);
uint32_t f16_to_f32(uint16_t a, float_status *fp_status);

uint16_t f16_to_uh(uint16_t op1, float_status *fp_status);
int16_t  f16_to_h(uint16_t op1, float_status *fp_status);
uint8_t  f16_to_ub(uint16_t op1, float_status *fp_status);
int8_t   f16_to_b(uint16_t op1, float_status *fp_status);

uint16_t uh_to_f16(uint16_t op1);
uint16_t h_to_f16(int16_t op1);
uint16_t ub_to_f16(uint8_t op1);
uint16_t b_to_f16(int8_t op1);

int32_t conv_sf_w(int32_t a, float_status *fp_status);
int16_t conv_hf_h(int16_t a, float_status *fp_status);
int32_t conv_w_sf(uint32_t a, float_status *fp_status);
int16_t conv_h_hf(uint16_t a, float_status *fp_status);

#endif
