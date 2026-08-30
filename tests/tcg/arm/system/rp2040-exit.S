/*
 * Raspberry Pi Pico host-exit behavior tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#ifndef EXIT_MODE
#error EXIT_MODE must select semihosting or Pico SDK exit
#endif

#define EXIT_MODE_SEMIHOST      0
#define EXIT_MODE_PICO_SDK      1
#define SRAM_END                0x20042000
#define SYS_EXIT_EXTENDED       0x20
#define ADP_STOPPED_APPLICATION_EXIT 0x20026
#define TEST_EXIT_STATUS        23

.section .vectors, "a", %progbits
vector_table:
    .word SRAM_END
    .word reset_handler + 1
    .word hardfault_handler + 1
    .word hardfault_handler + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
#if EXIT_MODE == EXIT_MODE_SEMIHOST
    movs r0, SYS_EXIT_EXTENDED
    ldr r1, =semihost_exit_block
    bkpt 0xab
#elif EXIT_MODE == EXIT_MODE_PICO_SDK
    movs r0, TEST_EXIT_STATUS
    bkpt 0
#else
#error Unsupported EXIT_MODE
#endif

1:
    b 1b

.thumb_func
hardfault_handler:
    b hardfault_handler

.section .rodata, "a", %progbits
.align 2
semihost_exit_block:
    .word ADP_STOPPED_APPLICATION_EXIT
    .word TEST_EXIT_STATUS
