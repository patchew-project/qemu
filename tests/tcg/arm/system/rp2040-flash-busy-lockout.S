/*
 * Raspberry Pi Pico multicore flash-busy lockout test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define CORE0_SRAM_VTOR        0x20000000
#define CORE0_SRAM_CODE        0x20000100
#define PAGE_BUFFER            0x20000400
#define CORE1_SRAM_VTOR        0x20001000
#define CORE1_STACK            0x20041000
#define UART0_DR               0x40034000
#define SIO_BASE               0xd0000000
#define SIO_FIFO_ST            0x50
#define SIO_FIFO_WR            0x54
#define SIO_FIFO_RD            0x58
#define SIO_SPINLOCK0          0x100
#define SIO_FIFO_ST_VLD        0x01
#define SIO_FIFO_ST_RDY        0x02
#define NVIC_ISER              0xe000e100
#define SCB_VTOR               0xe000ed08
#define SIO_IRQ_PROC1_MASK     0x00010000
#define SSI_BASE               0x18000000
#define SSI_SSIENR             0x08
#define SSI_SER                0x10
#define SSI_DR0                0x60
#define FLASH_ADDRESS          0x10101000
#define LOCK_REQUEST           0xfacefeed
#define LOCK_ACK               0xface0001
#define BAD_ACK                0xbad10003
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
    bl copy_core1_sram
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
    bl copy_and_run_core0_sram
    ldr r0, =fail_return_message
    b test_failed

.thumb_func
launch_core1:
    push {r4, r5, lr}
    ldr r4, =launch_sequence
    movs r5, 6

1:
    ldr r0, [r4]
    cmp r0, 0
    bne 2f
    bl fifo_drain
    sev
    ldr r0, [r4]

2:
    bl fifo_push
    sev
    bl fifo_pop
    ldr r1, [r4]
    cmp r0, r1
    bne fail_launch
    adds r4, 4
    subs r5, 1
    bne 1b
    pop {r4, r5, pc}

.thumb_func
copy_core1_sram:
    ldr r0, =core1_sram_start
    ldr r1, =CORE1_SRAM_VTOR
    ldr r2, =core1_sram_end

3:
    cmp r0, r2
    bhs 4f
    ldr r3, [r0]
    str r3, [r1]
    adds r0, 4
    adds r1, 4
    b 3b

4:
    bx lr

.thumb_func
claim_spinlock:
    ldr r0, =(SIO_BASE + SIO_SPINLOCK0)
    ldr r1, [r0]
    cmp r1, 0
    beq fail_spinlock
    bx lr

.thumb_func
copy_and_run_core0_sram:
    ldr r0, =core0_sram_start
    ldr r1, =CORE0_SRAM_CODE
    ldr r2, =core0_sram_end

5:
    cmp r0, r2
    bhs 6f
    ldr r3, [r0]
    str r3, [r1]
    adds r0, 4
    adds r1, 4
    b 5b

6:
    ldr r0, =CORE0_SRAM_CODE
    adds r0, 1
    bx r0

.thumb_func
fifo_push:
    push {r1, r2, r3, lr}
    ldr r1, =SIO_BASE

7:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_RDY
    tst r2, r3
    beq 7b
    str r0, [r1, SIO_FIFO_WR]
    pop {r1, r2, r3, pc}

.thumb_func
fifo_pop:
    push {r1, r2, r3, lr}
    ldr r1, =SIO_BASE

8:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 8b
    ldr r0, [r1, SIO_FIFO_RD]
    pop {r1, r2, r3, pc}

.thumb_func
fifo_drain:
    push {r0, r1, r2, r3, lr}
    ldr r1, =SIO_BASE

9:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 10f
    ldr r0, [r1, SIO_FIFO_RD]
    b 9b

10:
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

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

11:
    b 11b

.thumb_func
default_handler:
    ldr r0, =fail_exception_message
    b test_failed

.thumb_func
puts:
    push {r1, r2, lr}
    ldr r1, =UART0_DR

12:
    ldrb r2, [r0]
    cmp r2, 0
    beq 13f
    str r2, [r1]
    adds r0, 1
    b 12b

13:
    pop {r1, r2, pc}

.align 2
core0_sram_start:
    ldr r0, =CORE0_SRAM_VTOR
    ldr r1, =SRAM_END
    str r1, [r0]
    movs r1, 0
    str r1, [r0, 4]
    ldr r1, =core0_sram_hardfault
    ldr r2, =core0_sram_start
    subs r1, r2
    ldr r2, =CORE0_SRAM_CODE
    adds r1, r1, r2
    str r1, [r0, 12]
    ldr r0, =SCB_VTOR
    ldr r1, =CORE0_SRAM_VTOR
    str r1, [r0]

    ldr r0, =PAGE_BUFFER
    movs r1, 0x5a
    str r1, [r0]

    ldr r4, =SSI_BASE
    movs r0, 1
    str r0, [r4, SSI_SSIENR]
    str r0, [r4, SSI_SER]
    movs r0, 0x06
    str r0, [r4, SSI_DR0]
    movs r0, 0
    str r0, [r4, SSI_SER]

    movs r0, 1
    str r0, [r4, SSI_SER]
    movs r0, 0x02
    str r0, [r4, SSI_DR0]
    movs r0, 0x10
    str r0, [r4, SSI_DR0]
    str r0, [r4, SSI_DR0]
    movs r0, 0
    str r0, [r4, SSI_DR0]
    movs r0, 0x5a
    str r0, [r4, SSI_DR0]
    movs r0, 0
    str r0, [r4, SSI_SER]

    ldr r0, =FLASH_ADDRESS
    ldrb r0, [r0]
    ldr r0, =core0_sram_no_fault_message
    ldr r1, =core0_sram_start
    subs r0, r1
    ldr r1, =CORE0_SRAM_CODE
    adds r0, r1
    bl core0_sram_puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

14:
    b 14b

.thumb_func
core0_sram_hardfault:
    ldr r0, =core0_sram_ok_message
    ldr r1, =core0_sram_start
    subs r0, r1
    ldr r1, =CORE0_SRAM_CODE
    adds r0, r1
    bl core0_sram_puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

15:
    b 15b

.thumb_func
core0_sram_puts:
    ldr r1, =UART0_DR

16:
    ldrb r2, [r0]
    cmp r2, 0
    beq 17f
    str r2, [r1]
    adds r0, 1
    b 16b

17:
    bx lr

.align 2
core0_sram_ok_message:
    .asciz "PICO TCG FLASH BUSY LOCKOUT OK\n"
core0_sram_no_fault_message:
    .asciz "PICO TCG FLASH BUSY LOCKOUT FAIL: NO FAULT\n"

.ltorg
.align 2
core0_sram_end:

core1_sram_start:
    .word CORE1_STACK
    .word CORE1_SRAM_VTOR + (core1_sram_entry - core1_sram_start) + 1
    .word 0
    .word 0
    .space 0x80 - 16
    .word CORE1_SRAM_VTOR + (core1_sram_fifo_irq - core1_sram_start) + 1
    .space 0x100 - 0x84

.thumb_func
core1_sram_entry:
    ldr r0, =SCB_VTOR
    ldr r1, =CORE1_SRAM_VTOR
    str r1, [r0]
    ldr r0, =NVIC_ISER
    ldr r1, =SIO_IRQ_PROC1_MASK
    str r1, [r0]
    cpsie i

18:
    wfe
    b 18b

.thumb_func
core1_sram_fifo_irq:
    push {r0, r1, r2, r3, lr}
    bl core1_sram_fifo_pop
    ldr r1, =LOCK_REQUEST
    cmp r0, r1
    bne core1_sram_bad_request
    ldr r0, =LOCK_ACK
    bl core1_sram_fifo_push

19:
    wfe
    b 19b

core1_sram_bad_request:
    ldr r0, =BAD_ACK
    bl core1_sram_fifo_push

20:
    b 20b

.thumb_func
core1_sram_fifo_push:
    ldr r1, =SIO_BASE

21:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_RDY
    tst r2, r3
    beq 21b
    str r0, [r1, SIO_FIFO_WR]
    bx lr

.thumb_func
core1_sram_fifo_pop:
    ldr r1, =SIO_BASE

22:
    ldr r2, [r1, SIO_FIFO_ST]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 22b
    ldr r0, [r1, SIO_FIFO_RD]
    bx lr

.ltorg
.align 2
core1_sram_end:

.section .rodata, "a", %progbits
.align 2
launch_sequence:
    .word 0
    .word 0
    .word 1
    .word CORE1_SRAM_VTOR
    .word CORE1_STACK
    .word CORE1_SRAM_VTOR + (core1_sram_entry - core1_sram_start) + 1

start_message:
    .asciz "PICO TCG FLASH BUSY START\n"
launch_message:
    .asciz "PICO TCG FLASH BUSY CORE1 SRAM OK\n"
locked_message:
    .asciz "PICO TCG FLASH BUSY LOCKED\n"
fail_launch_message:
    .asciz "PICO TCG FLASH BUSY FAIL: LAUNCH\n"
fail_spinlock_message:
    .asciz "PICO TCG FLASH BUSY FAIL: SPINLOCK\n"
fail_fifo_message:
    .asciz "PICO TCG FLASH BUSY FAIL: FIFO\n"
fail_return_message:
    .asciz "PICO TCG FLASH BUSY FAIL: RETURN\n"
fail_exception_message:
    .asciz "PICO TCG FLASH BUSY FAIL: EXCEPTION\n"
