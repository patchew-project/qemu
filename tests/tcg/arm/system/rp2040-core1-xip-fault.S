/*
 * Raspberry Pi Pico core 1 XIP-busy instruction fault test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define SRAM_BASE              0x20000000
#define CORE1_STACK            0x20041000
#define UART0_DR               0x40034000
#define SIO_BASE               0xd0000000
#define SIO_CPUID              0x00
#define SIO_FIFO_ST            0x50
#define SIO_FIFO_WR            0x54
#define SIO_FIFO_RD            0x58
#define SIO_FIFO_ST_VLD        0x01
#define SIO_FIFO_ST_RDY        0x02
#define SCB_VTOR               0xe000ed08
#define SSI_BASE               0x18000000
#define SSI_CTRLR0             0x00
#define SSI_SSIENR             0x08
#define SSI_SER                0x10
#define SSI_RXFLR              0x24
#define SSI_DR0                0x60
#define SSI_CTRLR0_SPI_TXRX    0x00070000
#define CORE1_READY            0xc01e1001
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
    bl xip_puts
    ldr r0, =SRAM_BASE
    ldr r1, =sram_start
    ldr r2, =sram_end

1:
    cmp r1, r2
    bhs 2f
    ldr r3, [r1]
    str r3, [r0]
    adds r1, 4
    adds r0, 4
    b 1b

2:
    ldr r0, =SRAM_BASE
    adds r0, sram_core0_main - sram_start + 1
    bx r0

.thumb_func
default_handler:
    b default_handler

.thumb_func
xip_core1_loop:
    ldr r0, =CORE1_READY
    bl xip_fifo_push

3:
    b 3b

.thumb_func
xip_fifo_push:
    ldr r1, =(SIO_BASE + SIO_FIFO_ST)

4:
    ldr r2, [r1]
    movs r3, SIO_FIFO_ST_RDY
    tst r2, r3
    beq 4b
    ldr r1, =(SIO_BASE + SIO_FIFO_WR)
    str r0, [r1]
    bx lr

.thumb_func
xip_puts:
    ldr r2, =UART0_DR

5:
    ldrb r1, [r0]
    cmp r1, 0
    beq 6f
    str r1, [r2]
    adds r0, 1
    b 5b

6:
    bx lr

.align 2
sram_start:
    .word SRAM_END
    .word SRAM_BASE + (sram_core0_main - sram_start) + 1
    .word 0
    .word SRAM_BASE + (sram_hardfault - sram_start) + 1

.align 2
.thumb_func
sram_core0_main:
    ldr r0, =SCB_VTOR
    ldr r1, =SRAM_BASE
    str r1, [r0]
    bl sram_launch_core1
    bl sram_fifo_pop
    ldr r1, =CORE1_READY
    cmp r0, r1
    bne sram_fail_ready

    adr r0, sram_ready_message
    bl sram_puts
    bl sram_start_flash_program

7:
    b 7b

.thumb_func
sram_launch_core1:
    push {r4, r5, lr}
    adr r4, sram_launch_sequence
    movs r5, 6

8:
    ldr r0, [r4]
    cmp r0, 0
    bne 9f
    bl sram_fifo_drain
    sev
    ldr r0, [r4]

9:
    bl sram_fifo_push
    sev
    bl sram_fifo_pop
    ldr r1, [r4]
    cmp r0, r1
    bne sram_fail_launch
    adds r4, 4
    subs r5, 1
    bne 8b
    pop {r4, r5, pc}

.thumb_func
sram_fifo_push:
    ldr r1, =(SIO_BASE + SIO_FIFO_ST)

10:
    ldr r2, [r1]
    movs r3, SIO_FIFO_ST_RDY
    tst r2, r3
    beq 10b
    ldr r1, =(SIO_BASE + SIO_FIFO_WR)
    str r0, [r1]
    bx lr

.thumb_func
sram_fifo_pop:
    ldr r1, =(SIO_BASE + SIO_FIFO_ST)

11:
    ldr r2, [r1]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 11b
    ldr r1, =(SIO_BASE + SIO_FIFO_RD)
    ldr r0, [r1]
    bx lr

.thumb_func
sram_fifo_drain:
    push {r0, r1, r2, r3, lr}
    ldr r1, =(SIO_BASE + SIO_FIFO_ST)

12:
    ldr r2, [r1]
    movs r3, SIO_FIFO_ST_VLD
    tst r2, r3
    beq 13f
    ldr r0, =(SIO_BASE + SIO_FIFO_RD)
    ldr r0, [r0]
    b 12b

13:
    pop {r0, r1, r2, r3, pc}

.thumb_func
sram_start_flash_program:
    push {lr}
    ldr r4, =SSI_BASE
    movs r0, 0
    str r0, [r4, SSI_SSIENR]
    ldr r0, =SSI_CTRLR0_SPI_TXRX
    str r0, [r4, SSI_CTRLR0]
    movs r0, 1
    str r0, [r4, SSI_SSIENR]

    str r0, [r4, SSI_SER]
    movs r0, 0x06
    bl sram_ssi_txrx
    movs r0, 0
    str r0, [r4, SSI_SER]

    movs r0, 1
    str r0, [r4, SSI_SER]
    movs r0, 0x02
    bl sram_ssi_txrx
    movs r0, 0x00
    bl sram_ssi_txrx
    movs r0, 0x30
    bl sram_ssi_txrx
    movs r0, 0x00
    bl sram_ssi_txrx
    movs r0, 0xa5
    bl sram_ssi_txrx
    movs r0, 0
    str r0, [r4, SSI_SER]
    pop {pc}

.thumb_func
sram_ssi_txrx:
    ldr r2, =(SSI_BASE + SSI_DR0)
    str r0, [r2]
    ldr r3, =(SSI_BASE + SSI_RXFLR)

14:
    ldr r0, [r3]
    cmp r0, 0
    beq 14b
    ldr r0, [r2]
    bx lr

.align 2
.thumb_func
sram_hardfault:
    ldr r0, =SIO_BASE
    ldr r0, [r0, SIO_CPUID]
    cmp r0, 1
    bne sram_fail_core
    adr r0, sram_ok_message
    bl sram_puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

15:
    b 15b

sram_fail_launch:
    adr r0, sram_fail_launch_message
    b sram_test_failed

sram_fail_ready:
    adr r0, sram_fail_ready_message
    b sram_test_failed

sram_fail_core:
    adr r0, sram_fail_core_message

sram_test_failed:
    bl sram_puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

16:
    b 16b

.thumb_func
sram_puts:
    ldr r2, =UART0_DR

17:
    ldrb r1, [r0]
    cmp r1, 0
    beq 18f
    str r1, [r2]
    adds r0, 1
    b 17b

18:
    bx lr

.align 2
sram_launch_sequence:
    .word 0
    .word 0
    .word 1
    .word SRAM_BASE
    .word CORE1_STACK
    .word xip_core1_loop + 1

.align 2
sram_ready_message:
    .asciz "PICO TCG CORE1 XIP READY\n"
.align 2
sram_ok_message:
    .asciz "PICO TCG CORE1 XIP HARDFAULT OK\n"
.align 2
sram_fail_launch_message:
    .asciz "PICO TCG CORE1 XIP FAIL: LAUNCH\n"
.align 2
sram_fail_ready_message:
    .asciz "PICO TCG CORE1 XIP FAIL: READY\n"
.align 2
sram_fail_core_message:
    .asciz "PICO TCG CORE1 XIP FAIL: CPUID\n"

.ltorg
.align 2
sram_end:

.section .rodata, "a", %progbits
start_message:
    .asciz "PICO TCG CORE1 XIP START\n"
