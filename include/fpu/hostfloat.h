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

float64 float64_add(float64 a, float64 b, float_status *status);
float64 float64_sub(float64 a, float64 b, float_status *status);

#endif /* HOSTFLOAT_H */
