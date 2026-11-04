/*
 * RISC-V Bulk Normalization Algorithm (RVBNA)
 *
 * Copyright (c) 2026 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_RISCV_TCG_RVBNA_H
#define TARGET_RISCV_TCG_RVBNA_H

#include "fpu/softfloat.h"

float32 rvbna_final_f32_add(float32 partial, float32 accumulator,
                            float_status *fp_status);

float32 rvbna_dot_float8_e4m3_float8_e4m3(const float8_e4m3 a[],
                                          const float8_e4m3 b[],
                                          uint32_t n_prod,
                                          float_status *fp_status);
float32 rvbna_dot_float8_e4m3_float8_e5m2(const float8_e4m3 a[],
                                          const float8_e5m2 b[],
                                          uint32_t n_prod,
                                          float_status *fp_status);
float32 rvbna_dot_float8_e5m2_float8_e4m3(const float8_e5m2 a[],
                                          const float8_e4m3 b[],
                                          uint32_t n_prod,
                                          float_status *fp_status);
float32 rvbna_dot_float8_e5m2_float8_e5m2(const float8_e5m2 a[],
                                          const float8_e5m2 b[],
                                          uint32_t n_prod,
                                          float_status *fp_status);
float32 rvbna_dot_float16_float16(const float16 a[], const float16 b[],
                                  uint32_t n_prod,
                                  float_status *fp_status);
float32 rvbna_dot_bfloat16_bfloat16(const bfloat16 a[], const bfloat16 b[],
                                    uint32_t n_prod,
                                    float_status *fp_status);

#endif
