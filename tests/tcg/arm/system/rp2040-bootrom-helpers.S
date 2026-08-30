/*
 * Raspberry Pi Pico boot ROM integer and memory helper test.
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
#define ROM_FUNC_TABLE_PTR     0x00000014
#define ROM_TABLE_LOOKUP_PTR   0x00000018
#define ROM_FUNC_POPCOUNT32    0x3350
#define ROM_FUNC_CLZ32         0x334c
#define ROM_FUNC_CTZ32         0x3354
#define ROM_FUNC_REVERSE32     0x3352
#define ROM_FUNC_MEMSET        0x534d
#define ROM_FUNC_MEMCPY        0x434d
#define ROM_FUNC_MEMSET4       0x3453
#define ROM_FUNC_MEMCPY44      0x3443
#define TEST_BUFFER            0x20000000
#define SYS_EXIT               0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

.macro lookup code
    ldr r0, =\code
    bl rom_func_lookup
    cmp r0, 0
    bne 0f
    b fail_lookup
0:
    mov r4, r0
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

    lookup ROM_FUNC_POPCOUNT32
    ldr r0, =0xf0f00f0f
    blx r4
    fail_if_ne r0, 16, fail_bits

    lookup ROM_FUNC_CLZ32
    ldr r0, =0x00f00000
    blx r4
    fail_if_ne r0, 8, fail_bits

    lookup ROM_FUNC_CTZ32
    ldr r0, =0x00008000
    blx r4
    fail_if_ne r0, 15, fail_bits

    lookup ROM_FUNC_REVERSE32
    ldr r0, =0x80000001
    blx r4
    fail_if_ne r0, 0x80000001, fail_bits

    ldr r0, =bits_ok_message
    bl puts

    lookup ROM_FUNC_MEMSET
    ldr r0, =TEST_BUFFER
    movs r1, 0x5a
    movs r2, 4
    blx r4
    ldr r5, =TEST_BUFFER
    ldrb r0, [r5]
    fail_if_ne r0, 0x5a, fail_byte_memory
    ldrb r0, [r5, 3]
    fail_if_ne r0, 0x5a, fail_byte_memory

    ldr r5, =TEST_BUFFER + 0x20
    ldr r0, =0x11223344
    str r0, [r5]
    lookup ROM_FUNC_MEMCPY
    ldr r0, =TEST_BUFFER + 0x10
    ldr r1, =TEST_BUFFER + 0x20
    movs r2, 4
    blx r4
    ldr r5, =TEST_BUFFER + 0x10
    ldr r0, [r5]
    fail_if_ne r0, 0x11223344, fail_byte_memory

    ldr r0, =byte_memory_ok_message
    bl puts

    lookup ROM_FUNC_MEMSET4
    ldr r5, =TEST_BUFFER + 0x30
    ldr r0, =0xdeadbeef
    str r0, [r5, 8]
    ldr r0, =TEST_BUFFER + 0x30
    movs r1, 0x5a
    movs r2, 8
    blx r4
    ldr r5, =TEST_BUFFER + 0x30
    ldr r0, [r5]
    fail_if_ne r0, 0x5a5a5a5a, fail_word_memory
    ldr r0, [r5, 4]
    fail_if_ne r0, 0x5a5a5a5a, fail_word_memory
    ldr r0, [r5, 8]
    fail_if_ne r0, 0xdeadbeef, fail_word_memory

    ldr r5, =TEST_BUFFER + 0x40
    ldr r0, =0x55667788
    str r0, [r5]
    ldr r0, =0x99aabbcc
    str r0, [r5, 4]
    ldr r5, =TEST_BUFFER + 0x50
    ldr r0, =0xdeadbeef
    str r0, [r5, 8]
    lookup ROM_FUNC_MEMCPY44
    ldr r0, =TEST_BUFFER + 0x50
    ldr r1, =TEST_BUFFER + 0x40
    movs r2, 8
    blx r4
    ldr r5, =TEST_BUFFER + 0x50
    ldr r0, [r5]
    fail_if_ne r0, 0x55667788, fail_word_memory
    ldr r0, [r5, 4]
    fail_if_ne r0, 0x99aabbcc, fail_word_memory
    ldr r0, [r5, 8]
    fail_if_ne r0, 0xdeadbeef, fail_word_memory

    ldr r0, =word_memory_ok_message
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

fail_bits:
    ldr r0, =fail_bits_message
    b test_failed

fail_byte_memory:
    ldr r0, =fail_byte_memory_message
    b test_failed

fail_word_memory:
    ldr r0, =fail_word_memory_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

2:
    b 2b

.thumb_func
rom_func_lookup:
    push {r4, lr}
    mov r1, r0
    ldr r0, =ROM_FUNC_TABLE_PTR
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
    .asciz "PICO TCG BOOTROM HELPERS START\n"
bits_ok_message:
    .asciz "PICO TCG BOOTROM HELPERS BITS OK\n"
byte_memory_ok_message:
    .asciz "PICO TCG BOOTROM HELPERS BYTE MEMORY OK\n"
word_memory_ok_message:
    .asciz "PICO TCG BOOTROM HELPERS WORD MEMORY OK\n"
ok_message:
    .asciz "PICO TCG BOOTROM HELPERS OK\n"
fail_lookup_message:
    .asciz "PICO TCG BOOTROM HELPERS FAIL: LOOKUP\n"
fail_bits_message:
    .asciz "PICO TCG BOOTROM HELPERS FAIL: BITS\n"
fail_byte_memory_message:
    .asciz "PICO TCG BOOTROM HELPERS FAIL: BYTE MEMORY\n"
fail_word_memory_message:
    .asciz "PICO TCG BOOTROM HELPERS FAIL: WORD MEMORY\n"
fail_exception_message:
    .asciz "PICO TCG BOOTROM HELPERS FAIL: EXCEPTION\n"
