/*
 * Test EXECUTEing a non-mapped odd address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "asm-const.h"

#define CODE                                                                   \
    stringify_in_c(    lgrl %r1,odd_addr;)                                     \
    stringify_in_c(expected_old_pswa:;)                                        \
    stringify_in_c(    ex 0,%r1;)

#define DATA                                                                   \
    stringify_in_c(odd_addr:;)                                                 \
    stringify_in_c(    .quad 0xDDDDDDDDDDDDDDDD;)

#include "pgm-specification.inc"
