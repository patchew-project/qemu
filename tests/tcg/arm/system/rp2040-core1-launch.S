/*
 * Raspberry Pi Pico core 1 mask-ROM launch protocol test.
 *
 * Copyright 2026 Gilles Grimaud
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#define SRAM_END               0x20042000
#define CORE1_STACK            0x20041000
#define UART0_DR               0x40034000
#define SIO_BASE               0xd0000000
#define SIO_CPUID              0x00
#define SIO_FIFO_ST            0x50
#define SIO_FIFO_WR            0x54
#define SIO_FIFO_RD            0x58
#define SIO_FIFO_ST_VLD        0x01
#define SIO_FIFO_ST_RDY        0x02
#define CORE1_ACK              0x0000d01e
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

    ldr r4, =SIO_BASE
    ldr r0, [r4, SIO_CPUID]
    cmp r0, 0
    bne fail_core0_id

    ldr r6, =launch_sequence
    movs r7, 0

launch_next:
    ldr r0, [r6, r7]
    cmp r0, 0
    bne launch_send
    bl fifo_drain
    sev

launch_send:
    bl fifo_push
    bl fifo_pop
    ldr r1, [r6, r7]
    cmp r0, r1
    bne fail_echo
    adds r7, 4
    cmp r7, 24
    bne launch_next

    ldr r0, =echo_message
    bl puts

    bl fifo_pop
    ldr r1, =CORE1_ACK
    cmp r0, r1
    bne fail_ack

    ldr r0, =ack_message
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
    ldr r4, =SIO_BASE
    ldr r0, [r4, SIO_CPUID]
    cmp r0, 1
    bne core1_failed
    bl core1_send_ack

2:
    b 2b

.thumb_func
core1_send_ack:
    push {lr}
    ldr r0, =CORE1_ACK
    bl fifo_push
    pop {pc}

core1_failed:
    ldr r0, =0xbad10001
    bl fifo_push

3:
    b 3b

.thumb_func
fifo_drain:
    push {r0, r1, lr}

4:
    ldr r0, [r4, SIO_FIFO_ST]
    movs r1, SIO_FIFO_ST_VLD
    tst r0, r1
    beq 5f
    ldr r0, [r4, SIO_FIFO_RD]
    b 4b

5:
    pop {r0, r1, pc}

.thumb_func
fifo_push:
    push {r1, r2, lr}

6:
    ldr r1, [r4, SIO_FIFO_ST]
    movs r2, SIO_FIFO_ST_RDY
    tst r1, r2
    beq 6b
    str r0, [r4, SIO_FIFO_WR]
    sev
    pop {r1, r2, pc}

.thumb_func
fifo_pop:
    push {r1, lr}

7:
    ldr r1, [r4, SIO_FIFO_ST]
    movs r0, SIO_FIFO_ST_VLD
    tst r1, r0
    beq 7b
    ldr r0, [r4, SIO_FIFO_RD]
    pop {r1, pc}

fail_core0_id:
    ldr r0, =fail_core0_id_message
    b test_failed

fail_echo:
    ldr r0, =fail_echo_message
    b test_failed

fail_ack:
    ldr r0, =fail_ack_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

8:
    b 8b

.thumb_func
default_handler:
    ldr r0, =fail_exception_message
    b test_failed

.thumb_func
puts:
    ldr r2, =UART0_DR

9:
    ldrb r1, [r0]
    cmp r1, 0
    beq 10f
    str r1, [r2]
    adds r0, 1
    b 9b

10:
    bx lr

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
    .asciz "PICO TCG CORE1 START\n"
echo_message:
    .asciz "PICO TCG CORE1 ECHO OK\n"
ack_message:
    .asciz "PICO TCG CORE1 ACK OK\n"
ok_message:
    .asciz "PICO TCG CORE1 OK\n"
fail_core0_id_message:
    .asciz "PICO TCG CORE1 FAIL: CORE0 ID\n"
fail_echo_message:
    .asciz "PICO TCG CORE1 FAIL: ECHO\n"
fail_ack_message:
    .asciz "PICO TCG CORE1 FAIL: ACK\n"
fail_exception_message:
    .asciz "PICO TCG CORE1 FAIL: EXCEPTION\n"
