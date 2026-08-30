/*
 * RP2040 second-stage boot code for the Winbond W25Q080.
 *
 * Derived from boot2_w25q080.S in the Raspberry Pi Pico SDK.
 * Copyright (c) 2019-2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define PADS_QSPI_BASE          0x40020000
#define XIP_SSI_BASE            0x18000000
#define XIP_BASE                0x10000000
#define VTOR                    0xe000ed08

#define PADS_QSPI_SCLK          0x04
#define PADS_QSPI_SD0           0x08
#define PADS_QSPI_SD1           0x0c
#define PADS_QSPI_SD2           0x10
#define PADS_QSPI_SD3           0x14
#define PADS_QSPI_SCHMITT       0x02
#define PADS_QSPI_SCLK_8MA_FAST 0x21

#define SSI_CTRLR0              0x00
#define SSI_CTRLR1              0x04
#define SSI_SSIENR              0x08
#define SSI_BAUDR               0x14
#define SSI_SR                  0x28
#define SSI_DR0                 0x60
#define SSI_RX_SAMPLE_DLY       0xf0
#define SSI_SPI_CTRLR0          0xf4

#define SSI_SR_BUSY             0x01
#define SSI_SR_TFE              0x04

#define CTRL0_SPI_TXRX          0x00070000
#define CTRLR0_ENTER_XIP        0x005f0300
#define SPI_CTRLR0_ENTER_XIP    0x00002221
#define SPI_CTRLR0_XIP          0xa0002022

#define CMD_WRITE_STATUS        0x01
#define CMD_READ_STATUS         0x05
#define CMD_WRITE_ENABLE        0x06
#define CMD_READ_STATUS2        0x35
#define CMD_QUAD_IO_READ        0xeb
#define STATUS2_QUAD_ENABLE     0x02
#define MODE_CONTINUOUS_READ    0xa0

.section .boot2, "ax", %progbits
.global boot2
.type boot2, %function
.thumb_func
boot2:
    push {lr}

    ldr r3, =PADS_QSPI_BASE
    movs r0, PADS_QSPI_SCLK_8MA_FAST
    str r0, [r3, PADS_QSPI_SCLK]
    ldr r0, [r3, PADS_QSPI_SD0]
    movs r1, PADS_QSPI_SCHMITT
    bics r0, r1
    str r0, [r3, PADS_QSPI_SD0]
    str r0, [r3, PADS_QSPI_SD1]
    str r0, [r3, PADS_QSPI_SD2]
    str r0, [r3, PADS_QSPI_SD3]

    ldr r3, =XIP_SSI_BASE
    movs r1, 0
    str r1, [r3, SSI_SSIENR]
    movs r1, 2
    str r1, [r3, SSI_BAUDR]
    movs r1, 1
    movs r2, SSI_RX_SAMPLE_DLY
    str r1, [r3, r2]

    ldr r1, =CTRL0_SPI_TXRX
    str r1, [r3, SSI_CTRLR0]
    movs r1, 1
    str r1, [r3, SSI_SSIENR]

    movs r0, CMD_READ_STATUS2
    bl read_flash_sreg
    movs r2, STATUS2_QUAD_ENABLE
    cmp r0, r2
    beq skip_status_programming

    movs r1, CMD_WRITE_ENABLE
    str r1, [r3, SSI_DR0]
    bl wait_ssi_ready
    ldr r1, [r3, SSI_DR0]

    movs r1, CMD_WRITE_STATUS
    str r1, [r3, SSI_DR0]
    movs r0, 0
    str r0, [r3, SSI_DR0]
    str r2, [r3, SSI_DR0]
    bl wait_ssi_ready
    ldr r1, [r3, SSI_DR0]
    ldr r1, [r3, SSI_DR0]
    ldr r1, [r3, SSI_DR0]

1:
    movs r0, CMD_READ_STATUS
    bl read_flash_sreg
    movs r1, 1
    tst r0, r1
    bne 1b

skip_status_programming:
    movs r1, 0
    str r1, [r3, SSI_SSIENR]

    ldr r1, =CTRLR0_ENTER_XIP
    str r1, [r3, SSI_CTRLR0]
    movs r1, 0
    str r1, [r3, SSI_CTRLR1]
    ldr r1, =SPI_CTRLR0_ENTER_XIP
    ldr r0, =(XIP_SSI_BASE + SSI_SPI_CTRLR0)
    str r1, [r0]
    movs r1, 1
    str r1, [r3, SSI_SSIENR]
    movs r1, CMD_QUAD_IO_READ
    str r1, [r3, SSI_DR0]
    movs r1, MODE_CONTINUOUS_READ
    str r1, [r3, SSI_DR0]
    bl wait_ssi_ready

    movs r1, 0
    str r1, [r3, SSI_SSIENR]
    ldr r1, =SPI_CTRLR0_XIP
    ldr r0, =(XIP_SSI_BASE + SSI_SPI_CTRLR0)
    str r1, [r0]
    movs r1, 1
    str r1, [r3, SSI_SSIENR]

    pop {r0}
    cmp r0, 0
    beq vector_into_flash
    bx r0

vector_into_flash:
    ldr r0, =(XIP_BASE + 0x100)
    ldr r1, =VTOR
    str r0, [r1]
    ldmia r0, {r0, r1}
    msr msp, r0
    bx r1

wait_ssi_ready:
    push {r0, r1, lr}
2:
    ldr r1, [r3, SSI_SR]
    movs r0, SSI_SR_TFE
    tst r1, r0
    beq 2b
    movs r0, SSI_SR_BUSY
    tst r1, r0
    bne 2b
    pop {r0, r1, pc}

read_flash_sreg:
    push {r1, lr}
    str r0, [r3, SSI_DR0]
    str r0, [r3, SSI_DR0]
    bl wait_ssi_ready
    ldr r0, [r3, SSI_DR0]
    ldr r0, [r3, SSI_DR0]
    pop {r1, pc}

.align 2
.ltorg

    .space 252 - (. - boot2), 0
    .word 0
.size boot2, . - boot2
