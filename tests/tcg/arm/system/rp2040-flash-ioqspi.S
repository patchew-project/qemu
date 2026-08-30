/*
 * Raspberry Pi Pico IO_QSPI chip-select flash command test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_BASE               0x20000000
#define SRAM_END                0x20042000
#define UART0_DR                0x40034000
#define SSI_CTRLR0              0x18000000
#define SSI_SSIENR              0x18000008
#define SSI_RXFLR               0x18000024
#define SSI_DR0                 0x18000060
#define IO_QSPI_SS_CTRL         0x4001800c
#define XIP_TARGET              0x10001200
#define FLASH_TARGET_OFFSET     0x00001200
#define SYS_EXIT                0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define ADP_STOPPED_RUNTIME_ERROR    0x20023

.section .vectors, "a", %progbits
vector_table:
    .word SRAM_END
    .word reset_handler + 1
    .word default_handler + 1
    .word default_handler + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    ldr r0, =SRAM_BASE
    ldr r1, =ram_code_start
    ldr r2, =ram_code_end

copy_loop:
    cmp r1, r2
    bhs copy_done
    ldr r3, [r1]
    str r3, [r0]
    adds r0, 4
    adds r1, 4
    b copy_loop

copy_done:
    ldr r0, =SRAM_BASE + 1
    bx r0

.thumb_func
default_handler:
    b default_handler

.align 2
ram_code_start:
.thumb_func
ram_main:
    ldr r0, =start_message
    bl puts
    ldr r1, =SSI_SSIENR
    movs r0, 0
    str r0, [r1]
    ldr r1, =SSI_CTRLR0
    ldr r0, =0x00070000
    str r0, [r1]
    ldr r1, =SSI_SSIENR
    movs r0, 1
    str r0, [r1]

    bl write_enable
    bl cs_low
    movs r0, 0x20
    bl ssi_byte
    movs r0, 0x00
    bl ssi_byte
    movs r0, 0x12
    bl ssi_byte
    movs r0, 0x00
    bl ssi_byte
    bl cs_high
    bl read_status

    bl write_enable
    bl cs_low
    movs r0, 0x02
    bl ssi_byte
    movs r0, 0x00
    bl ssi_byte
    movs r0, 0x12
    bl ssi_byte
    movs r0, 0x00
    bl ssi_byte
    movs r0, 'Q'
    bl ssi_byte
    movs r0, 'S'
    bl ssi_byte
    movs r0, 'P'
    bl ssi_byte
    movs r0, 'I'
    bl ssi_byte
    bl cs_high
    bl read_status

    ldr r1, =XIP_TARGET
    ldrb r0, [r1]
    cmp r0, 'Q'
    bne test_failed
    ldrb r0, [r1, 1]
    cmp r0, 'S'
    bne test_failed
    ldrb r0, [r1, 2]
    cmp r0, 'P'
    bne test_failed
    ldrb r0, [r1, 3]
    cmp r0, 'I'
    bne test_failed

    ldr r0, =ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

.thumb_func
write_enable:
    push {lr}
    bl cs_low
    movs r0, 0x06
    bl ssi_byte
    bl cs_high
    pop {pc}

.thumb_func
read_status:
    push {lr}
    bl cs_low
    movs r0, 0x05
    bl ssi_byte
    bl cs_high
    pop {pc}

.thumb_func
ssi_byte:
    ldr r2, =SSI_DR0
    str r0, [r2]
    ldr r3, =SSI_RXFLR

2:
    ldr r0, [r3]
    cmp r0, 0
    beq 2b
    ldr r0, [r2]
    bx lr

.thumb_func
cs_low:
    ldr r1, =IO_QSPI_SS_CTRL
    movs r0, 2
    lsls r0, r0, 8
    str r0, [r1]
    bx lr

.thumb_func
cs_high:
    ldr r1, =IO_QSPI_SS_CTRL
    movs r0, 3
    lsls r0, r0, 8
    str r0, [r1]
    bx lr

test_failed:
    ldr r0, =fail_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

3:
    b 3b

.thumb_func
puts:
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
start_message:
    .asciz "PICO TCG IO_QSPI FLASH START\n"
ok_message:
    .asciz "PICO TCG IO_QSPI FLASH OK\n"
fail_message:
    .asciz "PICO TCG IO_QSPI FLASH FAIL\n"
.align 2
.ltorg
ram_code_end:
