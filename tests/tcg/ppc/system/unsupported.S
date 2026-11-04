/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Process Mission
 */

#include "macros.inc"

    .text
    .globl test_main
test_main:
    /* Valid memory prevents address faults from hiding decode errors */
    load_addr r4, scratch
    check_illegal fcpsgn f0, f0, f0
    check_illegal fcpsgn. f0, f0, f0
    check_illegal lfiwax f0, 0, r4
    check_illegal lfdp f0, 0(r4)
    check_illegal lfdpx f0, 0, r4
    check_illegal stfdp f0, 0(r4)
    check_illegal stfdpx f0, 0, r4
#ifdef E500MC
    check_illegal cmpb r11, r9, r10
    check_illegal prtyw r11, r9
    check_illegal prtyd r11, r9
#endif
    b pass

    .data
    .balign 16
scratch:
    .space 16
