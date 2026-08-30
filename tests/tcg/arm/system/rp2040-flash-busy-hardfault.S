/*
 * Raspberry Pi Pico XIP access while flash is busy test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_BASE               0x20000000
#define SRAM_VTOR               0x20001000
#define SRAM_END                0x20042000
#define VTOR                    0xe000ed08
#define UART0_DR                0x40034000
#define SSI_SER                 0x18000010
#define SSI_RXFLR               0x18000024
#define SSI_DR0                 0x18000060
#define XIP_TARGET              0x10001400
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
    ldr r0, =SRAM_VTOR
    ldr r1, =SRAM_END
    str r1, [r0]
    ldr r1, =SRAM_BASE + 1
    str r1, [r0, 4]
    movs r1, 0
    str r1, [r0, 8]
    adr r1, hardfault_handler
    adds r1, 1
    str r1, [r0, 12]
    ldr r1, =VTOR
    str r0, [r1]

    adr r0, start_message
    bl puts
    bl write_enable
    bl select_flash
    movs r0, 0x02
    bl ssi_byte
    movs r0, 0x00
    bl ssi_byte
    movs r0, 0x14
    bl ssi_byte
    movs r0, 0x00
    bl ssi_byte
    movs r0, 'X'
    bl ssi_byte
    bl deselect_flash

    ldr r1, =XIP_TARGET
faulting_access:
    ldrb r0, [r1]
    adr r0, fail_message
    b test_failed

.thumb_func
write_enable:
    push {lr}
    bl select_flash
    movs r0, 0x06
    bl ssi_byte
    bl deselect_flash
    pop {pc}

.thumb_func
select_flash:
    ldr r1, =SSI_SER
    movs r0, 1
    str r0, [r1]
    bx lr

.thumb_func
deselect_flash:
    ldr r1, =SSI_SER
    movs r0, 0
    str r0, [r1]
    bx lr

.thumb_func
ssi_byte:
    ldr r2, =SSI_DR0
    str r0, [r2]
    ldr r3, =SSI_RXFLR

1:
    ldr r0, [r3]
    cmp r0, 0
    beq 1b
    ldr r0, [r2]
    bx lr

.align 2
.thumb_func
hardfault_handler:
    adr r0, ok_message
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

2:
    b 2b

test_failed:
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
    .asciz "PICO TCG FLASH XIP BUSY START\n"
.align 2
ok_message:
    .asciz "PICO TCG FLASH XIP BUSY HARDFAULT OK\n"
.align 2
fail_message:
    .asciz "PICO TCG FLASH XIP BUSY FAIL: NO HARDFAULT\n"
.align 2
.ltorg
ram_code_end:
