/*
 * Test LPSWE from a non-doubleword aligned address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "asm-const.h"

#define CODE                                                                   \
    stringify_in_c(expected_old_pswa:;)                                        \
    stringify_in_c(    lpswe unaligned;)

#define DATA                                                                   \
    stringify_in_c(    .long 0;)                                               \
    stringify_in_c(unaligned:)                                                 \
    stringify_in_c(    .quad 0, 0;)

#include "pgm-specification.inc"
