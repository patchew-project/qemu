/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Process Mission
 */

#include "macros.inc"

    .text
    .globl test_main
test_main:
    load_value r9, 0x1122334455667788
    load_value r10, 0x1122994455660088
    cmpb r11, r9, r10
    check_value r11, 0xffff00ffffff00ff

    .macro parity value, word, doubleword
    load_value r9, \value
    prtyw r11, r9
    check_value r11, \word
#ifdef _LP64
    prtyd r11, r9
    check_value r11, \doubleword
#endif
    .endm

    /* Parity uses the least significant bit of each byte */
    parity 0x0101010100010000, 1, 1
    parity 0x0001010100010000, 0x100000001, 0
    parity 0x8080808002020202, 0, 0
    parity 0x0000000000000080, 0, 0
    parity 0x8000000000000000, 0, 0
    b pass
