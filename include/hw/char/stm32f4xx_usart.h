/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_STM32F4XX_USART_H
#define HW_STM32F4XX_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define USART_SR   0x00
#define USART_DR   0x04
#define USART_BRR  0x08
#define USART_CR1  0x0C
#define USART_CR2  0x10
#define USART_CR3  0x14
#define USART_GTPR 0x18

#define USART_SR_RESET 0x00C0

#define USART_SR_TXE  (1 << 7)
#define USART_SR_TC   (1 << 6)
#define USART_SR_RXNE (1 << 5)

#define USART_CR1_UE      (1 << 13)
#define USART_CR1_RXNEIE  (1 << 5)
#define USART_CR1_TE      (1 << 3)
#define USART_CR1_RE      (1 << 2)
#define USART_CR1_M       (1 << 12)
#define USART_CR1_TXEIE   (1 << 7)
#define USART_CR1_TCIE    (1 << 6)

#define USART_CR2_CLKEN   (1 << 11)
#define USART_CR2_LINEN   (1 << 14)

#define USART_CR3_SCEN    (1 << 5)
#define USART_CR3_HDSEL   (1 << 3)
#define USART_CR3_IREN    (1 << 1)

#define TYPE_STM32F4XX_USART "stm32f4xx-usart"
#define STM32F4XX_USART(obj) \
    OBJECT_CHECK(STM32F4XXUsartState, (obj), TYPE_STM32F4XX_USART)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t usart_sr;
    uint32_t usart_dr;
    uint32_t usart_brr;
    uint32_t usart_cr1;
    uint32_t usart_cr2;
    uint32_t usart_cr3;
    uint32_t usart_gtpr;

    CharBackend chr;
    qemu_irq irq;
} STM32F4XXUsartState;

#endif
