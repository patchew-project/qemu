/*
 * Raspberry Pi Pico flash command and NOR semantics test.
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
#define SSI_SER                 0x18000010
#define SSI_RXFLR               0x18000024
#define SSI_DR0                 0x18000060
#define XIP_TARGET              0x10001000
#define FLASH_TARGET_OFFSET     0x00001000
#define FLASH_STATUS_WIP        0x01
#define FLASH_STATUS_WEL        0x02
#define SYS_EXIT                0x18
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

    bl write_enable
    bl expect_wel
    ldr r0, =FLASH_TARGET_OFFSET
    bl erase_sector
    bl expect_busy_wel_clear
    bl expect_target_erased

    bl write_enable
    bl expect_wel
    ldr r0, =FLASH_TARGET_OFFSET
    ldr r1, =flash_message
    movs r2, 5
    bl program_data
    bl expect_busy_wel_clear
    bl expect_flash_message
    ldr r0, =program_ok_message
    bl puts

    bl write_enable
    ldr r0, =FLASH_TARGET_OFFSET
    bl erase_sector
    bl expect_busy_wel_clear
    bl expect_target_erased
    ldr r0, =erase_ok_message
    bl puts

    bl write_enable
    ldr r0, =FLASH_TARGET_OFFSET
    ldr r1, =flash_message
    movs r2, 5
    bl program_data
    bl expect_busy_wel_clear
    bl expect_flash_message

    ldr r0, =FLASH_TARGET_OFFSET + 8
    ldr r1, =no_wel_value
    movs r2, 1
    bl program_data
    bl read_status
    movs r1, FLASH_STATUS_WIP | FLASH_STATUS_WEL
    tst r0, r1
    beq 8f
    b fail_no_wel
8:
    ldr r1, =XIP_TARGET + 8
    ldrb r0, [r1]
    cmp r0, 0xff
    beq 9f
    b fail_no_wel
9:
    ldr r0, =no_wel_ok_message
    bl puts

    bl write_enable
    ldr r0, =FLASH_TARGET_OFFSET + 16
    ldr r1, =zero_value
    movs r2, 1
    bl program_data
    bl expect_busy_wel_clear
    bl write_enable
    ldr r0, =FLASH_TARGET_OFFSET + 16
    ldr r1, =erased_value
    movs r2, 1
    bl program_data
    bl expect_busy_wel_clear
    ldr r1, =XIP_TARGET + 16
    ldrb r0, [r1]
    cmp r0, 0
    beq 10f
    b fail_old_and_new
10:
    ldr r0, =old_and_new_ok_message
    bl puts

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
    bl select_flash
    movs r0, 0x06
    bl ssi_byte
    bl deselect_flash
    pop {pc}

.thumb_func
read_status:
    push {r4, lr}
    bl select_flash
    movs r0, 0x05
    bl ssi_byte
    mov r4, r0
    bl deselect_flash
    mov r0, r4
    pop {r4, pc}

.thumb_func
expect_wel:
    push {lr}
    bl read_status
    movs r1, FLASH_STATUS_WEL
    tst r0, r1
    beq fail_wel
    pop {pc}

.thumb_func
expect_busy_wel_clear:
    push {lr}
    bl read_status
    movs r1, FLASH_STATUS_WIP
    tst r0, r1
    beq fail_status
    movs r1, FLASH_STATUS_WEL
    tst r0, r1
    bne fail_status
    pop {pc}

.thumb_func
erase_sector:
    push {r4, lr}
    mov r4, r0
    bl select_flash
    movs r0, 0x20
    bl ssi_byte
    lsrs r0, r4, 16
    bl ssi_byte
    lsrs r0, r4, 8
    bl ssi_byte
    mov r0, r4
    bl ssi_byte
    bl deselect_flash
    pop {r4, pc}

.thumb_func
program_data:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl select_flash
    movs r0, 0x02
    bl ssi_byte
    lsrs r0, r4, 16
    bl ssi_byte
    lsrs r0, r4, 8
    bl ssi_byte
    mov r0, r4
    bl ssi_byte

program_loop:
    cmp r6, 0
    beq program_done
    ldrb r0, [r5]
    bl ssi_byte
    adds r5, 1
    subs r6, 1
    b program_loop

program_done:
    bl deselect_flash
    pop {r4, r5, r6, pc}

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

2:
    ldr r0, [r3]
    cmp r0, 0
    beq 2b
    ldr r0, [r2]
    bx lr

.thumb_func
expect_target_erased:
    ldr r1, =XIP_TARGET
    movs r2, 20

3:
    ldrb r0, [r1]
    cmp r0, 0xff
    bne fail_erase
    adds r1, 1
    subs r2, 1
    bne 3b
    bx lr

.thumb_func
expect_flash_message:
    ldr r0, =XIP_TARGET
    ldr r1, =flash_message
    movs r2, 5

4:
    ldrb r3, [r0]
    ldrb r4, [r1]
    cmp r3, r4
    bne fail_program
    adds r0, 1
    adds r1, 1
    subs r2, 1
    bne 4b
    bx lr

fail_wel:
    ldr r0, =fail_wel_message
    b test_failed

fail_status:
    ldr r0, =fail_status_message
    b test_failed

fail_program:
    ldr r0, =fail_program_message
    b test_failed

fail_erase:
    ldr r0, =fail_erase_message
    b test_failed

fail_no_wel:
    ldr r0, =fail_no_wel_message
    b test_failed

fail_old_and_new:
    ldr r0, =fail_old_and_new_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

5:
    b 5b

.thumb_func
puts:
    ldr r2, =UART0_DR

6:
    ldrb r1, [r0]
    cmp r1, 0
    beq 7f
    str r1, [r2]
    adds r0, 1
    b 6b

7:
    bx lr

.align 2
flash_message:
    .ascii "FLASH"
no_wel_value:
    .byte 0xa5
zero_value:
    .byte 0x00
erased_value:
    .byte 0xff
start_message:
    .asciz "PICO TCG FLASH COMMANDS START\n"
program_ok_message:
    .asciz "PICO TCG FLASH PROGRAM OK\n"
erase_ok_message:
    .asciz "PICO TCG FLASH ERASE OK\n"
no_wel_ok_message:
    .asciz "PICO TCG FLASH NO WEL OK\n"
old_and_new_ok_message:
    .asciz "PICO TCG FLASH OLD AND NEW OK\n"
ok_message:
    .asciz "PICO TCG FLASH COMMANDS OK\n"
fail_wel_message:
    .asciz "PICO TCG FLASH COMMANDS FAIL: WEL\n"
fail_status_message:
    .asciz "PICO TCG FLASH COMMANDS FAIL: STATUS\n"
fail_program_message:
    .asciz "PICO TCG FLASH COMMANDS FAIL: PROGRAM\n"
fail_erase_message:
    .asciz "PICO TCG FLASH COMMANDS FAIL: ERASE\n"
fail_no_wel_message:
    .asciz "PICO TCG FLASH COMMANDS FAIL: NO WEL\n"
fail_old_and_new_message:
    .asciz "PICO TCG FLASH COMMANDS FAIL: OLD AND NEW\n"
.align 2
.ltorg
ram_code_end:
