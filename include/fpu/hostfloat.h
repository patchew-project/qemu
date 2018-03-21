/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef HOSTFLOAT_H
#define HOSTFLOAT_H

#ifndef SOFTFLOAT_H
#error fpu/hostfloat.h must only be included from softfloat.h
#endif

float32 float32_add(float32 a, float32 b, float_status *status);
float32 float32_sub(float32 a, float32 b, float_status *status);
float32 float32_mul(float32 a, float32 b, float_status *status);
float32 float32_div(float32 a, float32 b, float_status *status);
float32 float32_muladd(float32 a, float32 b, float32 c, int f, float_status *s);
float32 float32_sqrt(float32 a, float_status *status);
int float32_compare(float32 a, float32 b, float_status *s);
int float32_compare_quiet(float32 a, float32 b, float_status *s);

float64 float64_add(float64 a, float64 b, float_status *status);
float64 float64_sub(float64 a, float64 b, float_status *status);
float64 float64_mul(float64 a, float64 b, float_status *status);
float64 float64_div(float64 a, float64 b, float_status *status);
float64 float64_muladd(float64 a, float64 b, float64 c, int f, float_status *s);
float64 float64_sqrt(float64 a, float_status *status);
int float64_compare(float64 a, float64 b, float_status *s);
int float64_compare_quiet(float64 a, float64 b, float_status *s);

float64 float32_to_float64(float32, float_status *status);

#endif /* HOSTFLOAT_H */
