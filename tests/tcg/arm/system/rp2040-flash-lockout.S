/*
 * Raspberry Pi Pico multicore flash lockout test.
 *
 * Copyright 2026 Gilles Grimaud
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define CORE1_STACK            0x20041000
#define PAGE_BUFFER            0x20000000
#define UART0_DR               0x40034000
#define SIO_BASE               0xd0000000
#define SIO_FIFO_ST            0x50
#define SIO_FIFO_WR            0x54
#define SIO_FIFO_RD            0x58
#define SIO_SPINLOCK0          0x100
#define SIO_FIFO_ST_VLD        0x01
#define SIO_FIFO_ST_RDY        0x02
#define NVIC_ISER              0xe000e100
#define SIO_IRQ_PROC1_MASK     0x00010000
#define DEBUG_BASE             0x5fff0000
#define DEBUG_CMD              0x00
#define DEBUG_ARG0             0x04
#define DEBUG_ARG1             0x08
#define DEBUG_ARG2             0x0c
#define DEBUG_ARG3             0x10
#define DEBUG_COUNT_ERASE      0x50
#define DEBUG_COUNT_PROGRAM    0x54
#define DEBUG_FLASH_ERASE      0x4552
#define DEBUG_FLASH_PROGRAM    0x5052
#define FLASH_OFFSET           0x00101000
#define FLASH_ADDRESS          0x10101000
#define LOCK_REQUEST           0xfacefeed
#define LOCK_ACK               0xface0001
#define RELEASE_REQUEST        0xfacebeef
#define RELEASE_ACK            0xface0002
#define BAD_ACK                0xbad10002
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
    .rept 28
    .word default_handler + 1
    .endr
    .word core1_fifo_irq + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    ldr r0, =start_message
    bl puts
    bl launch_core1

    ldr r0, =launch_message
    bl puts
    bl claim_spinlock
    ldr r0, =LOCK_REQUEST
    bl fifo_push
    sev
    ldr r0, =LOCK_ACK
    bl fifo_pop_expect

    ldr r0, =locked_message
    bl puts
    bl flash_erase_program_verify

    ldr r0, =flash_message
    bl puts
    ldr r0, =RELEASE_REQUEST
    bl fifo_push
    sev
    ldr r0, =RELEASE_ACK
    bl fifo_pop_expect
    bl release_spinlock

    ldr r0, =released_message
    bl puts
    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

.thumb_func
core1_entry:
    ldr r0, =NVIC_ISER
    ldr r1, =SIO_IRQ_PROC1_MASK
    str r1, [r0]
    cpsie i

2:
    wfe
    b 2b

.thumb_func
core1_fifo_irq:
    push {r0, r1, r2, r3, r4, lr}
    bl fifo_pop
    ldr r1, =LOCK_REQUEST
    cmp r0, r1
    bne core1_bad_request
    ldr r0, =LOCK_ACK
    bl fifo_push

3:
    wfe
    ldr r0, =SIO_BASE
    ldr r1, [r0, SIO_FIFO_ST]
    movs r0, SIO_FIFO_ST_VLD
    tst r1, r0
    beq 3b
    bl fifo_pop
    ldr r1, =RELEASE_REQUEST
    cmp r0, r1
    bne core1_bad_request
    ldr r0, =RELEASE_ACK
    bl fifo_push
    pop {r0, r1, r2, r3, r4, pc}

core1_bad_request:
    ldr r0, =BAD_ACK
    bl fifo_push

4:
    b 4b

.thumb_func
launch_core1:
    push {r4, r5, lr}
    ldr r4, =launch_sequence
    movs r5, 6

5:
    ldr r0, [r4]
    cmp r0, 0
    bne 6f
    bl fifo_drain
    sev
    ldr r0, [r4]

6:
    bl fifo_push
    sev
    bl fifo_pop
    ldr r1, [r4]
    cmp r0, r1
    bne fail_launch
    adds r4, 4
    subs r5, 1
    bne 5b
    pop {r4, r5, pc}

.thumb_func
claim_spinlock:
    ldr r0, =(SIO_BASE + SIO_SPINLOCK0)
    ldr r1, [r0]
    cmp r1, 0
    beq fail_spinlock
    bx lr

.thumb_func
release_spinlock:
    ldr r0, =(SIO_BASE + SIO_SPINLOCK0)
    movs r1, 0
    str r1, [r0]
    bx lr

.thumb_func
flash_erase_program_verify:
    push {r4, r5, r6, lr}
    ldr r0, =PAGE_BUFFER
    movs r1, 0
    movs r2, 0
    movs r3, 1
    lsls r3, r3, 8

7:
    strb r1, [r0, r2]
    adds r1, 1
    adds r2, 1
    cmp r2, r3
    bne 7b

    ldr r4, =DEBUG_BASE
    ldr r0, =FLASH_OFFSET
    str r0, [r4, DEBUG_ARG0]
    movs r0, 1
    lsls r0, r0, 12
    str r0, [r4, DEBUG_ARG1]
    movs r0, 0
    str r0, [r4, DEBUG_ARG2]
    movs r0, 0x20
    str r0, [r4, DEBUG_ARG3]
    ldr r0, =DEBUG_FLASH_ERASE
    str r0, [r4, DEBUG_CMD]

    ldr r0, =FLASH_ADDRESS
    movs r2, 0
    movs r3, 1
    lsls r3, r3, 8

8:
    ldrb r5, [r0, r2]
    movs r6, 0xff
    cmp r5, r6
    bne fail_flash
    adds r2, 1
    cmp r2, r3
    bne 8b

    ldr r0, =FLASH_OFFSET
    str r0, [r4, DEBUG_ARG0]
    ldr r0, =PAGE_BUFFER
    str r0, [r4, DEBUG_ARG1]
    movs r0, 1
    lsls r0, r0, 8
    str r0, [r4, DEBUG_ARG2]
    ldr r0, =DEBUG_FLASH_PROGRAM
    str r0, [r4, DEBUG_CMD]

    ldr r0, =FLASH_ADDRESS
    movs r1, 0
    movs r2, 0
    movs r3, 1
    lsls r3, r3, 8

9:
    ldrb r5, [r0, r2]
    cmp r5, r1
    bne fail_flash
    adds r1, 1
    adds r2, 1
    cmp r2, r3
    bne 9b

    ldr r0, [r4, DEBUG_COUNT_ERASE]
    cmp r0, 1
    bne fail_flash
    ldr r0, [r4, DEBUG_COUNT_PROGRAM]
    cmp r0, 1
    bne fail_flash
    pop {r4, r5, r6, pc}

.thumb_func
fifo_push:
    push {r1, r2, r3, lr}
    ldr r1, =SIO_BASE

10:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_RDY
    tst r2, r3
    beq 10b
    str r0, [r1, SIO_FIFO_WR]
    pop {r1, r2, r3, pc}

.thumb_func
fifo_pop:
    push {r1, r2, r3, lr}
    ldr r1, =SIO_BASE

11:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 11b
    ldr r0, [r1, SIO_FIFO_RD]
    pop {r1, r2, r3, pc}

.thumb_func
fifo_drain:
    push {r0, r1, r2, r3, lr}
    ldr r1, =SIO_BASE

12:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 13f
    ldr r0, [r1, SIO_FIFO_RD]
    b 12b

13:
    pop {r0, r1, r2, r3, pc}

.thumb_func
fifo_pop_expect:
    push {r4, lr}
    mov r4, r0
    bl fifo_pop
    cmp r0, r4
    bne fail_fifo
    pop {r4, pc}

fail_launch:
    ldr r0, =fail_launch_message
    b test_failed

fail_spinlock:
    ldr r0, =fail_spinlock_message
    b test_failed

fail_fifo:
    ldr r0, =fail_fifo_message
    b test_failed

fail_flash:
    ldr r0, =fail_flash_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

14:
    b 14b

.thumb_func
default_handler:
    ldr r0, =fail_exception_message
    b test_failed

.thumb_func
puts:
    push {r1, r2, lr}
    ldr r1, =UART0_DR

15:
    ldrb r2, [r0]
    cmp r2, 0
    beq 16f
    str r2, [r1]
    adds r0, 1
    b 15b

16:
    pop {r1, r2, pc}

.section .rodata, "a", %progbits
.align 2
launch_sequence:
    .word 0
    .word 0
    .word 1
    .word vector_table
    .word CORE1_STACK
    .word core1_entry + 1

start_message:
    .asciz "PICO TCG FLASH LOCKOUT START\n"
launch_message:
    .asciz "PICO TCG FLASH LOCKOUT CORE1 OK\n"
locked_message:
    .asciz "PICO TCG FLASH LOCKOUT LOCKED\n"
flash_message:
    .asciz "PICO TCG FLASH LOCKOUT PROGRAM OK\n"
released_message:
    .asciz "PICO TCG FLASH LOCKOUT RELEASED\n"
ok_message:
    .asciz "PICO TCG FLASH LOCKOUT OK\n"
fail_launch_message:
    .asciz "PICO TCG FLASH LOCKOUT FAIL: LAUNCH\n"
fail_spinlock_message:
    .asciz "PICO TCG FLASH LOCKOUT FAIL: SPINLOCK\n"
fail_fifo_message:
    .asciz "PICO TCG FLASH LOCKOUT FAIL: FIFO\n"
fail_flash_message:
    .asciz "PICO TCG FLASH LOCKOUT FAIL: FLASH\n"
fail_exception_message:
    .asciz "PICO TCG FLASH LOCKOUT FAIL: EXCEPTION\n"
