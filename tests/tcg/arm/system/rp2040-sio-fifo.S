/*
 * Raspberry Pi Pico SIO FIFO status test.
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
#define SIO_CPUID              0x00
#define SIO_FIFO_ST            0x50
#define SIO_FIFO_RD            0x58
#define SIO_FIFO_ST_VLD        0x01
#define SIO_FIFO_ST_RDY        0x02
#define SIO_FIFO_ST_ROE        0x08
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

    ldr r4, =SIO_BASE
    ldr r0, [r4, SIO_CPUID]
    cmp r0, 0
    bne fail_cpuid

    ldr r0, [r4, SIO_FIFO_ST]
    movs r1, SIO_FIFO_ST_RDY
    tst r0, r1
    beq fail_rdy

    /* The mask ROM on core 1 may have queued its initial ready word. */
    movs r1, SIO_FIFO_ST_VLD
    tst r0, r1
    beq fifo_empty
    ldr r0, [r4, SIO_FIFO_RD]
    cmp r0, 0
    bne fail_ready_word

fifo_empty:
    ldr r0, [r4, SIO_FIFO_ST]
    movs r1, SIO_FIFO_ST_VLD
    tst r0, r1
    bne fail_vld

    ldr r0, [r4, SIO_FIFO_RD]
    ldr r0, [r4, SIO_FIFO_ST]
    movs r1, SIO_FIFO_ST_ROE
    tst r0, r1
    beq fail_roe

    str r1, [r4, SIO_FIFO_ST]
    ldr r0, [r4, SIO_FIFO_ST]
    tst r0, r1
    bne fail_roe_clear

    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_cpuid:
    ldr r0, =fail_cpuid_message
    b test_failed

fail_rdy:
    ldr r0, =fail_rdy_message
    b test_failed

fail_vld:
    ldr r0, =fail_vld_message
    b test_failed

fail_ready_word:
    ldr r0, =fail_ready_word_message
    b test_failed

fail_roe:
    ldr r0, =fail_roe_message
    b test_failed

fail_roe_clear:
    ldr r0, =fail_roe_clear_message

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
    .asciz "PICO TCG SIO FIFO START\n"
ok_message:
    .asciz "PICO TCG SIO FIFO OK\n"
fail_cpuid_message:
    .asciz "PICO TCG SIO FIFO FAIL: CPUID\n"
fail_rdy_message:
    .asciz "PICO TCG SIO FIFO FAIL: RDY\n"
fail_vld_message:
    .asciz "PICO TCG SIO FIFO FAIL: VLD\n"
fail_ready_word_message:
    .asciz "PICO TCG SIO FIFO FAIL: READY WORD\n"
fail_roe_message:
    .asciz "PICO TCG SIO FIFO FAIL: ROE\n"
fail_roe_clear_message:
    .asciz "PICO TCG SIO FIFO FAIL: ROE CLEAR\n"
fail_exception_message:
    .asciz "PICO TCG SIO FIFO FAIL: EXCEPTION\n"
