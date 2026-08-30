/*
 * Raspberry Pi Pico DMA memory transfer test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define UART0_DR               0x40034000
#define DMA_BASE               0x50000000
#define DMA_READ_ADDR          0x000
#define DMA_WRITE_ADDR         0x004
#define DMA_TRANS_COUNT        0x008
#define DMA_CTRL_TRIG          0x00c
#define DMA_INTR               0x400
#define DMA_CTRL_WORD_COPY     0x001f8039
#define DMA_CTRL_WORD_FILL     0x001f8029
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

    ldr r5, =copy_source
    ldr r0, =0x11223344
    str r0, [r5, #0]
    ldr r0, =0xaabbccdd
    str r0, [r5, #4]
    ldr r0, =0x01020304
    str r0, [r5, #8]
    ldr r0, =0xfeedc0de
    str r0, [r5, #12]
    ldr r5, =fill_source
    ldr r0, =0x55aa55aa
    str r0, [r5]

    ldr r4, =DMA_BASE

    ldr r0, =copy_source
    str r0, [r4, DMA_READ_ADDR]
    ldr r0, =copy_destination
    str r0, [r4, DMA_WRITE_ADDR]
    movs r0, 4
    str r0, [r4, DMA_TRANS_COUNT]
    ldr r0, =DMA_CTRL_WORD_COPY
    str r0, [r4, DMA_CTRL_TRIG]

    ldr r5, =copy_source
    ldr r6, =copy_destination
    movs r3, 4

copy_check_loop:
    ldr r0, [r5]
    ldr r1, [r6]
    cmp r0, r1
    bne fail_copy
    adds r5, 4
    adds r6, 4
    subs r3, 1
    bne copy_check_loop

    ldr r6, =DMA_BASE + DMA_INTR
    ldr r0, [r6]
    movs r1, 1
    tst r0, r1
    beq fail_irq
    str r1, [r6]
    ldr r0, [r6]
    tst r0, r1
    bne fail_irq

    ldr r0, =copy_ok_message
    bl puts

    ldr r0, =fill_source
    str r0, [r4, DMA_READ_ADDR]
    ldr r0, =fill_destination
    str r0, [r4, DMA_WRITE_ADDR]
    movs r0, 4
    str r0, [r4, DMA_TRANS_COUNT]
    ldr r0, =DMA_CTRL_WORD_FILL
    str r0, [r4, DMA_CTRL_TRIG]

    ldr r5, =fill_destination
    ldr r1, =0x55aa55aa
    movs r3, 4

fill_check_loop:
    ldr r0, [r5]
    cmp r0, r1
    bne fail_fill
    adds r5, 4
    subs r3, 1
    bne fill_check_loop

    ldr r0, =fill_ok_message
    bl puts
    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_copy:
    ldr r0, =fail_copy_message
    b test_failed

fail_fill:
    ldr r0, =fail_fill_message
    b test_failed

fail_irq:
    ldr r0, =fail_irq_message

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

.section .bss, "aw", %nobits
.align 2
copy_source:
    .space 16
copy_destination:
    .space 16
fill_source:
    .space 4
fill_destination:
    .space 16

.section .rodata, "a", %progbits
start_message:
    .asciz "PICO TCG DMA MEMORY START\n"
copy_ok_message:
    .asciz "PICO TCG DMA MEMORY COPY OK\n"
fill_ok_message:
    .asciz "PICO TCG DMA MEMORY FILL OK\n"
ok_message:
    .asciz "PICO TCG DMA MEMORY OK\n"
fail_copy_message:
    .asciz "PICO TCG DMA MEMORY FAIL: COPY\n"
fail_fill_message:
    .asciz "PICO TCG DMA MEMORY FAIL: FILL\n"
fail_irq_message:
    .asciz "PICO TCG DMA MEMORY FAIL: IRQ\n"
fail_exception_message:
    .asciz "PICO TCG DMA MEMORY FAIL: EXCEPTION\n"
