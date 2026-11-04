/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Process Mission
 */

#include "macros.inc"

    .macro seed_fpscr
    mtfsf 255, f2
    mtfsfi 6, 1                 /* ZE */
    mtfsfi 7, 2                 /* RN */
    .endm

    .macro check_fpscr expected
    mffs f1
    stfd f1, 8(r4)
    lwz r11, 12(r4)
    load32 r12, \expected
    cmpw r11, r12
    bne fail
    .endm

    .macro test_mtfsfi field, value, w, record, expected
    seed_fpscr
    .if \record
    mtfsfi. \field, \value, \w
    .else
    mtfsfi \field, \value, \w
    .endif
    check_fpscr \expected
    .endm

    .macro test_mtfsf mask, w, l, record, expected
    seed_fpscr
    .if \record
    mtfsf. \mask, f0, \l, \w
    .else
    mtfsf \mask, f0, \l, \w
    .endif
    check_fpscr \expected
    .endm

    .text
    .globl test_main
test_main:
    load_addr r4, scratch
    lfd f0, 0(r4)               /* Source: upper word 3, lower word 1 */
    lfd f2, 16(r4)              /* Zero for a reproducible initial FPSCR */

    .irp w, 0, 1
    .irp record, 0, 1
    test_mtfsfi 6, 4, \w, \record, 0x42
    test_mtfsfi 7, 1, \w, \record, 0x11
    .endr
    .endr

    .irp w, 0, 1
    .irp l, 0, 1
    .irp record, 0, 1
    /* L must not override the ordinary field mask, and W is ignored */
    test_mtfsf 0, \w, \l, \record, 0x12
    test_mtfsf 1, \w, \l, \record, 0x11
    test_mtfsf 2, \w, \l, \record, 0x02
    test_mtfsf 255, \w, \l, \record, 0x01
    .endr
    .endr
    .endr
    b pass

    .data
    .balign 8
scratch:
    .long 3, 1
    .space 16
