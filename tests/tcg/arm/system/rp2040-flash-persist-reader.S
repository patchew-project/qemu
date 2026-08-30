/*
 * Raspberry Pi Pico persistent flash reader test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END                0x20042000
#define UART0_DR                0x40034000
#define XIP_TARGET              0x10001000
#define SYS_EXIT                0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

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
    ldr r0, =XIP_TARGET
    ldr r1, =expected_message
    movs r2, 5

check_loop:
    ldrb r3, [r0]
    ldrb r4, [r1]
    cmp r3, r4
    bne test_failed
    adds r0, 1
    adds r1, 1
    subs r2, 1
    bne check_loop

    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

test_failed:
    ldr r0, =fail_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

2:
    b 2b

.thumb_func
default_handler:
    b default_handler

.thumb_func
puts:
    ldr r2, =UART0_DR

3:
    ldrb r1, [r0]
    cmp r1, 0
    beq 4f
    str r1, [r2]
    adds r0, 1
    b 3b

4:
    bx lr

expected_message:
    .ascii "FLASH"
ok_message:
    .asciz "PICO TCG FLASH PERSIST OK\n"
fail_message:
    .asciz "PICO TCG FLASH PERSIST FAIL\n"
