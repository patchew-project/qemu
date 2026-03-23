/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_KVX_IEEE_H
#define HEXAGON_KVX_IEEE_H

#include "fpu/softfloat.h"

/* Hexagon canonical NaN */
#define FP32_DEF_NAN      0x7FFFFFFF
#define FP16_DEF_NAN      0x7FFF

#define signF32UI(a) ((bool)((uint32_t)(a) >> 31))
#define signF16UI(a) ((bool)((uint16_t)(a) >> 15))

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

/* IEEE BFloat instructions */

#define fp_mult_sf_bf(A, B) \
    fp_mult_sf_sf(((uint32_t)(A)) << 16, ((uint32_t)(B)) << 16, &env->fp_status)
#define fp_add_sf_bf(A, B) \
    fp_add_sf_sf(((uint32_t)(A)) << 16, ((uint32_t)(B)) << 16, &env->fp_status)
#define fp_sub_sf_bf(A, B) \
    fp_sub_sf_sf(((uint32_t)(A)) << 16, ((uint32_t)(B)) << 16, &env->fp_status)

uint32_t fp_mult_sf_bf_acc(uint16_t op1, uint16_t op2, uint32_t acc,
                           float_status *fp_status);

#define bf_to_sf(A) (((uint32_t)(A)) << 16)

#define fp_min_bf(A, B) ({ \
    uint32_t _bf_res = fp_min_sf(bf_to_sf(A), bf_to_sf(B), &env->fp_status); \
    (uint16_t)((_bf_res >> 16) & 0xffff); \
})

#define fp_max_bf(A, B) ({ \
    uint32_t _bf_res = fp_max_sf(bf_to_sf(A), bf_to_sf(B), &env->fp_status); \
    (uint16_t)((_bf_res >> 16) & 0xffff); \
})

static inline uint16_t sf_to_bf(int32_t A)
{
    uint32_t rslt = A;
    if ((rslt & 0x1FFFF) == 0x08000) {
        /* do not round up if exactly .5 and even already */
    } else if ((rslt & 0x8000) == 0x8000) {
        rslt += 0x8000; /* rounding to nearest number */
    }
    rslt = float32_is_any_nan(A) ? FP32_DEF_NAN : rslt;
    return rslt >> 16;
}

#endif
