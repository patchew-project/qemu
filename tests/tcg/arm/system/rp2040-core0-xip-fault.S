/*
 * Raspberry Pi Pico core 0 XIP-busy instruction fault test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define SRAM_BASE              0x20000000
#define UART0_DR               0x40034000
#define SCB_VTOR               0xe000ed08
#define SSI_BASE               0x18000000
#define SSI_CTRLR0             0x00
#define SSI_SSIENR             0x08
#define SSI_SER                0x10
#define SSI_RXFLR              0x24
#define SSI_DR0                0x60
#define SSI_CTRLR0_SPI_TXRX    0x00070000
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
    adds r0, 1
    bx r0

.thumb_func
default_handler:
    b default_handler

.thumb_func
xip_probe:
    ldr r0, =xip_no_fault_message
    bl xip_puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

3:
    b 3b

.thumb_func
xip_puts:
    ldr r2, =UART0_DR

4:
    ldrb r1, [r0]
    cmp r1, 0
    beq 5f
    str r1, [r2]
    adds r0, 1
    b 4b

5:
    bx lr

.align 2
sram_start:
    ldr r0, =SRAM_BASE
    ldr r1, =SRAM_END
    str r1, [r0]
    adr r1, sram_entry
    adds r1, 1
    str r1, [r0, 4]
    movs r1, 0
    str r1, [r0, 8]
    adr r1, sram_hardfault
    adds r1, 1
    str r1, [r0, 12]
    ldr r2, =SCB_VTOR
    str r0, [r2]

.align 2
.thumb_func
sram_entry:
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
    movs r0, 0x20
    bl sram_ssi_txrx
    movs r0, 0x00
    bl sram_ssi_txrx
    movs r0, 0x5a
    bl sram_ssi_txrx
    movs r0, 0
    str r0, [r4, SSI_SER]

    ldr r0, =xip_probe
    adds r0, 1
    bx r0

.thumb_func
sram_ssi_txrx:
    ldr r2, =(SSI_BASE + SSI_DR0)
    str r0, [r2]
    ldr r3, =(SSI_BASE + SSI_RXFLR)

6:
    ldr r0, [r3]
    cmp r0, 0
    beq 6b
    ldr r0, [r2]
    bx lr

.align 2
.thumb_func
sram_hardfault:
    adr r0, sram_ok_message
    bl sram_puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

7:
    b 7b

.thumb_func
sram_puts:
    ldr r2, =UART0_DR

8:
    ldrb r1, [r0]
    cmp r1, 0
    beq 9f
    str r1, [r2]
    adds r0, 1
    b 8b

9:
    bx lr

.align 2
sram_ok_message:
    .asciz "PICO TCG CORE0 XIP HARDFAULT OK\n"

.ltorg
.align 2
sram_end:

.section .rodata, "a", %progbits
start_message:
    .asciz "PICO TCG CORE0 XIP START\n"
xip_no_fault_message:
    .asciz "PICO TCG CORE0 XIP FAIL: NO FAULT\n"
