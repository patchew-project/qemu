/*
 * Raspberry Pi Pico SIO hardware divider test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define UART0_DR               0x40034000
#define SIO_BASE               0xd0000000
#define SIO_DIV_UDIVIDEND      0x60
#define SIO_DIV_UDIVISOR       0x64
#define SIO_DIV_SDIVIDEND      0x68
#define SIO_DIV_SDIVISOR       0x6c
#define SIO_DIV_QUOTIENT       0x70
#define SIO_DIV_REMAINDER      0x74
#define SIO_DIV_CSR            0x78
#define SIO_DIV_CSR_READY_DIRTY 0x03
#define SIO_DIV_CSR_READY      0x01
#define SYS_EXIT               0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

.macro fail_if_ne reg, expected, target
    ldr r2, =\expected
    cmp \reg, r2
    beq 0f
    b \target
0:
.endm

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
    ldr r4, =SIO_BASE

    /* Unsigned: 100 / 7 = 14 remainder 2. */
    movs r0, 100
    str r0, [r4, SIO_DIV_UDIVIDEND]
    movs r0, 7
    str r0, [r4, SIO_DIV_UDIVISOR]
    ldr r0, [r4, SIO_DIV_CSR]
    fail_if_ne r0, SIO_DIV_CSR_READY_DIRTY, fail_unsigned
    ldr r0, [r4, SIO_DIV_REMAINDER]
    fail_if_ne r0, 2, fail_unsigned
    ldr r0, [r4, SIO_DIV_CSR]
    fail_if_ne r0, SIO_DIV_CSR_READY_DIRTY, fail_unsigned
    ldr r0, [r4, SIO_DIV_QUOTIENT]
    fail_if_ne r0, 14, fail_unsigned
    ldr r0, [r4, SIO_DIV_CSR]
    fail_if_ne r0, SIO_DIV_CSR_READY, fail_unsigned

    /* Signed: -100 / 7 = -14 remainder -2. */
    ldr r0, =-100
    str r0, [r4, SIO_DIV_SDIVIDEND]
    movs r0, 7
    str r0, [r4, SIO_DIV_SDIVISOR]
    ldr r0, [r4, SIO_DIV_REMAINDER]
    fail_if_ne r0, -2, fail_signed
    ldr r0, [r4, SIO_DIV_QUOTIENT]
    fail_if_ne r0, -14, fail_signed

    /* Division by zero follows the RP2040 result contract. */
    ldr r0, =0x1234
    str r0, [r4, SIO_DIV_UDIVIDEND]
    movs r0, 0
    str r0, [r4, SIO_DIV_UDIVISOR]
    ldr r0, [r4, SIO_DIV_REMAINDER]
    fail_if_ne r0, 0x1234, fail_divzero
    ldr r0, [r4, SIO_DIV_QUOTIENT]
    fail_if_ne r0, 0xffffffff, fail_divzero

    /* Result writes restore a saved divider context and set DIRTY. */
    ldr r0, =0xaabbccdd
    str r0, [r4, SIO_DIV_REMAINDER]
    ldr r0, =0x11223344
    str r0, [r4, SIO_DIV_QUOTIENT]
    ldr r0, [r4, SIO_DIV_CSR]
    fail_if_ne r0, SIO_DIV_CSR_READY_DIRTY, fail_restore
    ldr r0, [r4, SIO_DIV_REMAINDER]
    fail_if_ne r0, 0xaabbccdd, fail_restore
    ldr r0, [r4, SIO_DIV_QUOTIENT]
    fail_if_ne r0, 0x11223344, fail_restore
    ldr r0, [r4, SIO_DIV_CSR]
    fail_if_ne r0, SIO_DIV_CSR_READY, fail_restore

    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_unsigned:
    ldr r0, =fail_unsigned_message
    b test_failed

fail_signed:
    ldr r0, =fail_signed_message
    b test_failed

fail_divzero:
    ldr r0, =fail_divzero_message
    b test_failed

fail_restore:
    ldr r0, =fail_restore_message

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
    .asciz "PICO TCG SIO DIVIDER START\n"
ok_message:
    .asciz "PICO TCG SIO DIVIDER OK\n"
fail_unsigned_message:
    .asciz "PICO TCG SIO DIVIDER FAIL: UNSIGNED\n"
fail_signed_message:
    .asciz "PICO TCG SIO DIVIDER FAIL: SIGNED\n"
fail_divzero_message:
    .asciz "PICO TCG SIO DIVIDER FAIL: DIVZERO\n"
fail_restore_message:
    .asciz "PICO TCG SIO DIVIDER FAIL: RESTORE\n"
fail_exception_message:
    .asciz "PICO TCG SIO DIVIDER FAIL: EXCEPTION\n"
