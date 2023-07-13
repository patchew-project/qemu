/*
 * Carry-less multiply
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef CRYPTO_CLMUL_H
#define CRYPTO_CLMUL_H

#include "qemu/int128.h"

/**
 * clmul_8x8_low:
 *
 * Perform eight 8x8->8 carry-less multiplies.
 */
uint64_t clmul_8x8_low_gen(uint64_t, uint64_t);

/**
 * clmul_8x4_even:
 *
 * Perform four 8x8->16 carry-less multiplies.
 * The odd bytes of the inputs are ignored.
 */
uint64_t clmul_8x4_even_gen(uint64_t, uint64_t);

/**
 * clmul_8x4_odd:
 *
 * Perform four 8x8->16 carry-less multiplies.
 * The even bytes of the inputs are ignored.
 */
uint64_t clmul_8x4_odd_gen(uint64_t, uint64_t);

/**
 * clmul_8x8_even:
 *
 * Perform eight 8x8->16 carry-less multiplies.
 * The odd bytes of the inputs are ignored.
 */
Int128 clmul_8x8_even_gen(Int128, Int128);

/**
 * clmul_8x8_odd:
 *
 * Perform eight 8x8->16 carry-less multiplies.
 * The even bytes of the inputs are ignored.
 */
Int128 clmul_8x8_odd_gen(Int128, Int128);

/**
 * clmul_8x8_packed:
 *
 * Perform eight 8x8->16 carry-less multiplies.
 */
Int128 clmul_8x8_packed_gen(uint64_t, uint64_t);

#include "host/crypto/clmul.h"

#endif /* CRYPTO_CLMUL_H */
