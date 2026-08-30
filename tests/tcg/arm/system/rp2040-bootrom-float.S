/*
 * Raspberry Pi Pico boot ROM floating-point table test.
 *
 * Copyright 2026 Gilles Grimaud
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define UART0_DR               0x40034000
#define ROM_DATA_TABLE_PTR     0x00000016
#define ROM_TABLE_LOOKUP_PTR   0x00000018
#define ROM_DATA_FLOAT_TABLE   0x4653
#define ROM_DATA_DOUBLE_TABLE  0x4453
#define FP_TABLE_FADD          0x00
#define FP_TABLE_FMUL          0x08
#define FP_TABLE_FDIV          0x0c
#define FP_TABLE_FLOAT2DOUBLE  0x7c
#define SYS_EXIT               0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

.macro lookup_data code, dst
    ldr r0, =\code
    bl rom_data_lookup
    cmp r0, 0
    bne 0f
    b fail_lookup
0:
    mov \dst, r0
.endm

.macro load_table_func basereg, offset
    mov r4, \basereg
    adds r4, \offset
    ldrb r3, [r4]
    ldrb r5, [r4, 1]
    lsls r5, 8
    orrs r3, r5
    ldrb r5, [r4, 2]
    lsls r5, 16
    orrs r3, r5
    ldrb r5, [r4, 3]
    lsls r5, 24
    orrs r3, r5
    mov r4, r3
.endm

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

    lookup_data ROM_DATA_FLOAT_TABLE, r6

    /* 1.25f + 2.5f = 3.75f. */
    load_table_func r6, FP_TABLE_FADD
    ldr r0, =0x3fa00000
    ldr r1, =0x40200000
    blx r4
    fail_if_ne r0, 0x40700000, fail_single

    /* 3.75f * 2.0f = 7.5f. */
    load_table_func r6, FP_TABLE_FMUL
    ldr r0, =0x40700000
    ldr r1, =0x40000000
    blx r4
    fail_if_ne r0, 0x40f00000, fail_single

    /* 7.5f / 3.0f = 2.5f. */
    load_table_func r6, FP_TABLE_FDIV
    ldr r0, =0x40f00000
    ldr r1, =0x40400000
    blx r4
    fail_if_ne r0, 0x40200000, fail_single

    /* Convert 1.25f to 0x3ff4000000000000. */
    load_table_func r6, FP_TABLE_FLOAT2DOUBLE
    ldr r0, =0x3fa00000
    blx r4
    fail_if_ne r0, 0x00000000, fail_single
    fail_if_ne r1, 0x3ff40000, fail_single

    ldr r0, =single_ok_message
    bl puts

    lookup_data ROM_DATA_DOUBLE_TABLE, r7

    /* 1.5 + 4.0 = 5.5. */
    load_table_func r7, FP_TABLE_FADD
    movs r0, 0
    ldr r1, =0x3ff80000
    movs r2, 0
    ldr r3, =0x40100000
    blx r4
    fail_if_ne r0, 0x00000000, fail_double
    fail_if_ne r1, 0x40160000, fail_double

    /* The SD entry at 0x7c converts 1.5 to the float 1.5f. */
    load_table_func r7, FP_TABLE_FLOAT2DOUBLE
    movs r0, 0
    ldr r1, =0x3ff80000
    blx r4
    fail_if_ne r0, 0x3fc00000, fail_double

    ldr r0, =double_ok_message
    bl puts
    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_lookup:
    ldr r0, =fail_lookup_message
    b test_failed

fail_single:
    ldr r0, =fail_single_message
    b test_failed

fail_double:
    ldr r0, =fail_double_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

2:
    b 2b

.thumb_func
rom_data_lookup:
    push {r4, lr}
    mov r1, r0
    ldr r0, =ROM_DATA_TABLE_PTR
    ldrh r0, [r0]
    ldr r4, =ROM_TABLE_LOOKUP_PTR
    ldrh r4, [r4]
    blx r4
    pop {r4, pc}

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
    .asciz "PICO TCG BOOTROM FLOAT START\n"
single_ok_message:
    .asciz "PICO TCG BOOTROM FLOAT SF OK\n"
double_ok_message:
    .asciz "PICO TCG BOOTROM FLOAT SD OK\n"
ok_message:
    .asciz "PICO TCG BOOTROM FLOAT OK\n"
fail_lookup_message:
    .asciz "PICO TCG BOOTROM FLOAT FAIL: LOOKUP\n"
fail_single_message:
    .asciz "PICO TCG BOOTROM FLOAT FAIL: SF\n"
fail_double_message:
    .asciz "PICO TCG BOOTROM FLOAT FAIL: SD\n"
fail_exception_message:
    .asciz "PICO TCG BOOTROM FLOAT FAIL: EXCEPTION\n"
