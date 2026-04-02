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

#endif
