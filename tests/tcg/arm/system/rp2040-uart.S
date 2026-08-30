/*
 * Raspberry Pi Pico UART boot smoke test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END 0x20042000
#define UART0_DR 0x40034000
#define GPIO0_CTRL 0x40014004
#define GPIO_FUNC_UART 2
#define SYS_EXIT 0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026

.section .vectors, "a", %progbits
vector_table:
    .word SRAM_END
    .word reset_handler + 1
    .word default_handler + 1
    .word default_handler + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    ldr r0, =GPIO0_CTRL
    movs r1, GPIO_FUNC_UART
    str r1, [r0]
    ldr r0, =UART0_DR
    ldr r1, =message

1:
    ldrb r2, [r1]
    cmp r2, 0
    beq 2f
    str r2, [r0]
    adds r1, 1
    b 1b

2:
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

3:
    b 3b

.thumb_func
default_handler:
    b default_handler

.section .rodata, "a", %progbits
message:
    .asciz "PICO TCG UART OK\n"
