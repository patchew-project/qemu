/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Process Mission
 */

#include "macros.inc"

    .section .text.start, "ax"
    .globl _start
_start:
    /* Map CCSR at VA 0xfe000000 to PA 0xfe0000000 in TLB1[1] */
    load32 r3, 0x10010000
    mtspr SPR_BOOKE_MAS0, r3    /* MAS0 */
    load32 r3, 0xc0000500
    mtspr SPR_BOOKE_MAS1, r3    /* MAS1: valid, protected, 1 MiB */
    load32 r3, 0xfe00000a
    mtspr SPR_BOOKE_MAS2, r3    /* MAS2: cache inhibited, guarded */
    load32 r3, 0xe000003f
    mtspr SPR_BOOKE_MAS3, r3    /* MAS3: RPN and permissions */
    li r3, 15
    mtspr SPR_BOOKE_MAS7, r3    /* MAS7 */
    tlbwe
    isync

    load_addr r3, vectors
    mtspr SPR_BOOKE_IVPR, r3    /* IVPR */
    li r3, 0x600
    mtspr SPR_BOOKE_IVOR6, r3   /* IVOR6: program exception */
    li r14, 0                   /* No program exception expected */

#ifdef _LP64
    load32 r3, 0x80002000
#else
    li r3, 0x2000
#endif
    mtmsr r3                    /* Enable FP and select computation mode */
    isync
    b test_main

    .section .vectors, "ax"
vectors:
    .space 0x600
program_exception:
    mfspr r3, SPR_SRR0          /* SRR0 must name the illegal instruction */
#ifdef _LP64
    cmpd r3, r14
#else
    cmpw r3, r14
#endif
    bne unexpected_exception
    mfspr r3, SPR_BOOKE_ESR     /* ESR must contain only PIL */
    load32 r5, 0x08000000
    cmpw r3, r5
    bne unexpected_exception
    mtspr SPR_SRR0, r15
    rfi
unexpected_exception:
    b fail

    .text
    .globl pass, fail
pass:
    load_addr r3, pass_message
    b report
fail:
    load_addr r3, fail_message
report:
    load32 r4, 0xfe004500       /* UART0 */
    rlwinm r4, r4, 0, 0, 31     /* Zero extend the address on 64-bit cores */
1:
    lbz r5, 0(r3)
    cmpwi r5, 0
    beq 2f
    stb r5, 0(r4)
    addi r3, r3, 1
    b 1b
2:
    /* GPIO pin 0 is the ppce500 power-off line */
    load32 r4, 0xfe0ff000
    rlwinm r4, r4, 0, 0, 31
    load32 r5, 0x80000000
    stw r5, 0(r4)               /* Direction: output */
    stw r5, 8(r4)               /* Data: assert pin 0 */
3:
    b 3b

    .section .rodata
pass_message:
    .asciz "ISA:PASS\n"
fail_message:
    .asciz "ISA:FAIL\n"
