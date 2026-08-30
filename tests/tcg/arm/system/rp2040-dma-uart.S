/*
 * Raspberry Pi Pico UART-paced DMA transfer test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.syntax unified
.cpu cortex-m0plus
.thumb

#ifndef UART_INDEX
#error UART_INDEX must select UART0 or UART1
#elif UART_INDEX == 0
#define UART_BASE              0x40034000
#define DREQ_UART_TX           20
#define DREQ_UART_RX           21
#define UART_NAME              "UART0"
#elif UART_INDEX == 1
#define UART_BASE              0x40038000
#define DREQ_UART_TX           22
#define DREQ_UART_RX           23
#define UART_NAME              "UART1"
#else
#error Unsupported UART_INDEX
#endif

#define SRAM_END               0x20042000
#define UART_LCR_H             0x2c
#define UART_CR                0x30
#define UART_DMACR             0x48
#define UART_CR_ENABLED        0x301
#define UART_CR_LOOPBACK       0x381
#define DMA_BASE               0x50000000
#define DMA_READ_ADDR          0x000
#define DMA_WRITE_ADDR         0x004
#define DMA_TRANS_COUNT        0x008
#define DMA_CTRL_TRIG          0x00c
#define DMA_INTR               0x400
#define DMA_INTE0              0x404
#define DMA_CTRL_EN            0x00000001
#define DMA_CTRL_INCR_R        0x00000010
#define DMA_CTRL_INCR_W        0x00000020
#define DMA_CTRL_TX            (DMA_CTRL_EN | DMA_CTRL_INCR_R | \
                                (DREQ_UART_TX << 15))
#define DMA_CTRL_RX            (DMA_CTRL_EN | DMA_CTRL_INCR_W | \
                                (DREQ_UART_RX << 15))
#define DMA_IRQ_0              11
#define NVIC_ISER              0xe000e100
#define NVIC_ICER              0xe000e180
#define NVIC_ICPR              0xe000e280
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
    .rept 12
    .word default_handler + 1
    .endr
    .rept DMA_IRQ_0
    .word default_handler + 1
    .endr
    .word dma_irq_handler + 1

.section .text, "ax", %progbits
.thumb_func
.global reset_handler
reset_handler:
    cpsid i
    ldr r0, =UART_BASE
    movs r1, 0x10
    str r1, [r0, UART_LCR_H]
    ldr r1, =UART_CR_ENABLED
    str r1, [r0, UART_CR]
    movs r1, 3
    str r1, [r0, UART_DMACR]

    ldr r0, =NVIC_ICER
    movs r2, 0
    mvns r2, r2
    str r2, [r0]
    ldr r0, =NVIC_ICPR
    str r2, [r0]
    ldr r0, =NVIC_ISER
    ldr r1, =(1 << DMA_IRQ_0)
    str r1, [r0]
    cpsie i

    ldr r0, =start_message
    movs r1, START_MESSAGE_LEN
    bl dma_tx

    ldr r0, =UART_BASE
    ldr r1, =UART_CR_LOOPBACK
    str r1, [r0, UART_CR]
    ldr r0, =hello_message
    movs r1, 5
    bl dma_tx
    ldr r0, =UART_BASE
    ldr r1, =UART_CR_ENABLED
    str r1, [r0, UART_CR]
    ldr r0, =newline_message
    bl puts

    ldr r0, =rx_buffer
    movs r1, 5
    bl dma_rx

    ldr r0, =rx_buffer
    ldr r1, =hello_message
    movs r2, 5

check_loop:
    ldrb r3, [r0]
    ldrb r4, [r1]
    cmp r3, r4
    bne fail_data
    adds r0, 1
    adds r1, 1
    subs r2, 1
    bne check_loop

    ldr r0, =ok_message
    movs r1, OK_MESSAGE_LEN
    bl dma_tx
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_APPLICATION_EXIT
    bkpt 0xab

1:
    b 1b

fail_data:
    ldr r0, =fail_data_message
    b test_failed

fail_irq:
    ldr r0, =fail_irq_message
    b test_failed

.thumb_func
default_handler:
    ldr r0, =fail_exception_message

test_failed:
    bl puts
    movs r0, SYS_EXIT
    ldr r1, =ADP_STOPPED_RUNTIME_ERROR
    bkpt 0xab

2:
    b 2b

.thumb_func
dma_tx:
    push {r4, lr}
    mov r4, r1
    bl clear_dma_irq
    ldr r2, =DMA_BASE
    str r0, [r2, DMA_READ_ADDR]
    ldr r3, =UART_BASE
    str r3, [r2, DMA_WRITE_ADDR]
    str r4, [r2, DMA_TRANS_COUNT]
    ldr r3, =DMA_CTRL_TX
    str r3, [r2, DMA_CTRL_TRIG]
    bl wait_dma_irq
    pop {r4, pc}

.thumb_func
dma_rx:
    push {r4, lr}
    mov r4, r1
    bl clear_dma_irq
    ldr r2, =DMA_BASE
    ldr r3, =UART_BASE
    str r3, [r2, DMA_READ_ADDR]
    str r0, [r2, DMA_WRITE_ADDR]
    str r4, [r2, DMA_TRANS_COUNT]
    ldr r3, =DMA_CTRL_RX
    str r3, [r2, DMA_CTRL_TRIG]
    bl wait_dma_irq
    pop {r4, pc}

.thumb_func
clear_dma_irq:
    ldr r2, =irq_flag
    movs r3, 0
    str r3, [r2]
    ldr r2, =DMA_BASE + DMA_INTR
    movs r3, 1
    str r3, [r2]
    ldr r2, =DMA_BASE + DMA_INTE0
    str r3, [r2]
    bx lr

.thumb_func
wait_dma_irq:
    ldr r2, =irq_flag

3:
    ldr r3, [r2]
    movs r4, 1
    tst r3, r4
    bne 4f
    b 3b

4:
    bx lr

.thumb_func
dma_irq_handler:
    ldr r0, =DMA_BASE + DMA_INTR
    ldr r1, [r0]
    movs r2, 1
    tst r1, r2
    beq fail_irq
    str r1, [r0]
    ldr r0, =irq_flag
    str r1, [r0]
    bx lr

.thumb_func
puts:
    ldr r2, =UART_BASE

5:
    ldrb r1, [r0]
    cmp r1, 0
    beq 6f
    str r1, [r2]
    adds r0, 1
    b 5b

6:
    bx lr

.section .bss, "aw", %nobits
.align 2
irq_flag:
    .word 0
rx_buffer:
    .space 8

.section .rodata, "a", %progbits
start_message:
    .ascii "PICO TCG DMA " UART_NAME " START\n"
start_message_end:
hello_message:
    .ascii "HELLO"
newline_message:
    .asciz "\n"
ok_message:
    .ascii "PICO TCG DMA " UART_NAME " TX/RX OK\n"
ok_message_end:
.equ START_MESSAGE_LEN, start_message_end - start_message
.equ OK_MESSAGE_LEN, ok_message_end - ok_message
fail_data_message:
    .asciz "PICO TCG DMA " UART_NAME " FAIL: DATA\n"
fail_irq_message:
    .asciz "PICO TCG DMA " UART_NAME " FAIL: IRQ\n"
fail_exception_message:
    .asciz "PICO TCG DMA " UART_NAME " FAIL: EXCEPTION\n"
