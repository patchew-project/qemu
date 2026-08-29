/*
 * Raspberry Pi Pico boot smoke test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END 0x20042000
#define SYS_EXIT 0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026

.section .vectors, "a", %progbits
vector_table:
    .word SRAM_END
    .word reset_handler + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b
