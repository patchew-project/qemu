/*
 * Test STRL to a non-word aligned address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "asm-const.h"

#define CODE                                                                   \
    stringify_in_c(expected_old_pswa:;)                                        \
    stringify_in_c(    strl %r1,unaligned;)

#define DATA                                                                   \
    stringify_in_c(    .short 0;)                                              \
    stringify_in_c(unaligned:;)                                                \
    stringify_in_c(    .long 0;)

#include "pgm-specification.inc"
