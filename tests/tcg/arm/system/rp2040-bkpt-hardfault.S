/*
 * Raspberry Pi Pico BKPT-to-HardFault test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define UART0_DR               0x40034000
#define HARDFAULT_EXCEPTION    3
#define SYS_EXIT               0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

.section .vectors, "a", %progbits
.global vector_table
vector_table:
    .word SRAM_END
    .word reset_handler + 1
    .word default_handler + 1
    .word hardfault_handler + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    ldr r0, =start_message
    bl puts
    bkpt 0

    ldr r0, =fail_return_message
    b test_failed

.thumb_func
hardfault_handler:
    mrs r0, ipsr
    cmp r0, HARDFAULT_EXCEPTION
    bne fail_exception

    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_exception:
    ldr r0, =fail_exception_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

2:
    b 2b

.thumb_func
default_handler:
    ldr r0, =fail_vector_message
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
    .asciz "PICO TCG BKPT START\n"
ok_message:
    .asciz "PICO TCG BKPT HARDFAULT OK\n"
fail_return_message:
    .asciz "PICO TCG BKPT FAIL: RETURNED\n"
fail_exception_message:
    .asciz "PICO TCG BKPT FAIL: IPSR\n"
fail_vector_message:
    .asciz "PICO TCG BKPT FAIL: VECTOR\n"
