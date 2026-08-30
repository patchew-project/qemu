/*
 * Raspberry Pi Pico timer alarm interrupt test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define TEST_FLAG              0x20000000
#define UART0_DR               0x40034000
#define TIMER_BASE             0x40054000
#define TIMER_ALARM0           0x10
#define TIMER_RAWL             0x28
#define TIMER_INTR             0x34
#define TIMER_INTE             0x38
#define NVIC_ISER              0xe000e100
#define SYS_EXIT               0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

.section .vectors, "a", %progbits
.global vector_table
vector_table:
    .word SRAM_END
    .word reset_handler + 1
    .word default_handler + 1
    .word default_handler + 1
    .rept 12
    .word default_handler + 1
    .endr
    .word timer_irq + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    ldr r0, =start_message
    bl puts

    ldr r0, =TEST_FLAG
    movs r1, 0
    str r1, [r0]

    ldr r0, =NVIC_ISER
    movs r1, 1
    str r1, [r0]

    ldr r4, =TIMER_BASE
    str r1, [r4, TIMER_INTR]
    str r1, [r4, TIMER_INTE]
    ldr r2, [r4, TIMER_RAWL]
    ldr r3, =1000
    adds r2, r2, r3
    str r2, [r4, TIMER_ALARM0]

1:
    ldr r0, =TEST_FLAG
    ldr r1, [r0]
    cmp r1, 0
    beq 1b

    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

2:
    b 2b

.thumb_func
timer_irq:
    ldr r0, =TIMER_BASE
    movs r1, 1
    str r1, [r0, TIMER_INTR]
    ldr r0, =TEST_FLAG
    str r1, [r0]
    bx lr

.thumb_func
default_handler:
    ldr r0, =fail_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

3:
    b 3b

.thumb_func
puts:
    ldr r2, =UART0_DR

4:
    ldrb r1, [r0]
    cmp r1, 0
    beq 5f
    str r1, [r2]
    adds r0, 1
    b 4b

5:
    bx lr

.section .rodata, "a", %progbits
start_message:
    .asciz "PICO TCG TIMER START\n"
ok_message:
    .asciz "PICO TCG TIMER OK\n"
fail_message:
    .asciz "PICO TCG TIMER FAIL\n"
