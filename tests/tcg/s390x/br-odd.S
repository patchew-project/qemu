/*
 * Test BRanching to a non-mapped odd address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "asm-const.h"

#define CODE                                                                   \
    stringify_in_c(    lgrl %r1,addr;)                                         \
    stringify_in_c(    br %r1;)

#define DATA                                                                   \
    stringify_in_c(addr:;)                                                     \
    stringify_in_c(    .quad 0xDDDDDDDDDDDDDDDD;)

#define EXPECTED_OLD_PSWA 0xDDDDDDDDDDDDDDDD

#include "pgm-specification.inc"
