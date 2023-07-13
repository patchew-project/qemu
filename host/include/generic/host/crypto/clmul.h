/*
 * No host specific carry-less multiply acceleration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GENERIC_HOST_CRYPTO_CLMUL_H
#define GENERIC_HOST_CRYPTO_CLMUL_H

/* Defer everything to the generic routines. */
#define clmul_8x8_low           clmul_8x8_low_gen
#define clmul_8x4_even          clmul_8x4_even_gen
#define clmul_8x4_odd           clmul_8x4_odd_gen
#define clmul_8x8_even          clmul_8x8_even_gen
#define clmul_8x8_odd           clmul_8x8_odd_gen
#define clmul_8x8_packed        clmul_8x8_packed_gen

#define clmul_16x2_even         clmul_16x2_even_gen
#define clmul_16x2_odd          clmul_16x2_odd_gen
#define clmul_16x4_even         clmul_16x4_even_gen
#define clmul_16x4_odd          clmul_16x4_odd_gen

#endif /* GENERIC_HOST_CRYPTO_CLMUL_H */
