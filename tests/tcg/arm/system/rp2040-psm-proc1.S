/*
 * Raspberry Pi Pico PSM PROC1 power-state test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define UART0_DR               0x40034000
#define PSM_BASE               0x40010000
#define PSM_FRCE_OFF           0x04
#define PSM_DONE               0x0c
#define PSM_SET_ALIAS          0x2000
#define PSM_CLR_ALIAS          0x3000
#define PSM_PROC1              0x00010000
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

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    ldr r0, =start_message
    bl puts

    ldr r4, =(PSM_BASE + PSM_FRCE_OFF)
    ldr r5, =PSM_PROC1
    ldr r6, =(PSM_BASE + PSM_DONE)

    ldr r0, [r4]
    tst r0, r5
    bne fail_initial
    ldr r0, [r6]
    tst r0, r5
    beq fail_initial

    ldr r0, =initial_message
    bl puts

    ldr r0, =(PSM_BASE + PSM_SET_ALIAS + PSM_FRCE_OFF)
    str r5, [r0]
    ldr r0, [r4]
    tst r0, r5
    beq fail_force_off
    ldr r0, [r6]
    tst r0, r5
    bne fail_force_off

    ldr r0, =off_message
    bl puts

    ldr r0, =(PSM_BASE + PSM_CLR_ALIAS + PSM_FRCE_OFF)
    str r5, [r0]
    ldr r0, [r4]
    tst r0, r5
    bne fail_restart
    ldr r0, [r6]
    tst r0, r5
    beq fail_restart

    ldr r0, =on_message
    bl puts
    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_initial:
    ldr r0, =fail_initial_message
    b test_failed

fail_force_off:
    ldr r0, =fail_force_off_message
    b test_failed

fail_restart:
    ldr r0, =fail_restart_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

2:
    b 2b

.thumb_func
default_handler:
    ldr r0, =fail_exception_message
    b test_failed

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

.section .rodata, "a", %progbits
start_message:
    .asciz "PICO TCG PSM PROC1 START\n"
initial_message:
    .asciz "PICO TCG PSM PROC1 INITIAL ON\n"
off_message:
    .asciz "PICO TCG PSM PROC1 FORCED OFF\n"
on_message:
    .asciz "PICO TCG PSM PROC1 RESTARTED\n"
ok_message:
    .asciz "PICO TCG PSM PROC1 OK\n"
fail_initial_message:
    .asciz "PICO TCG PSM PROC1 FAIL: INITIAL\n"
fail_force_off_message:
    .asciz "PICO TCG PSM PROC1 FAIL: FORCE OFF\n"
fail_restart_message:
    .asciz "PICO TCG PSM PROC1 FAIL: RESTART\n"
fail_exception_message:
    .asciz "PICO TCG PSM PROC1 FAIL: EXCEPTION\n"
