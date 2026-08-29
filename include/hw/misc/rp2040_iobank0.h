/*
 * RP2040 GPIO IO bank emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_IOBANK0_H
#define HW_MISC_RP2040_IOBANK0_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_IOBANK0 "rp2040-iobank0"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040IoBank0State, RP2040_IOBANK0)

#define RP2040_IOBANK0_BASE 0x40014000
#define RP2040_IOBANK0_SIZE 0x4000
#define RP2040_IOBANK0_NUM_GPIOS 30
#define RP2040_IOBANK0_IRQ_BANKS 4

struct RP2040IoBank0State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq proc0_irq;
    qemu_irq proc1_irq;
    qemu_irq uart0_pin[2];
    qemu_irq uart1_pin[2];

    uint32_t ctrl[RP2040_IOBANK0_NUM_GPIOS];
    uint32_t intr[RP2040_IOBANK0_IRQ_BANKS];
    uint32_t proc0_inte[RP2040_IOBANK0_IRQ_BANKS];
    uint32_t proc0_intf[RP2040_IOBANK0_IRQ_BANKS];
    uint32_t proc1_inte[RP2040_IOBANK0_IRQ_BANKS];
    uint32_t proc1_intf[RP2040_IOBANK0_IRQ_BANKS];
    uint32_t dormant_wake_inte[RP2040_IOBANK0_IRQ_BANKS];
    uint32_t dormant_wake_intf[RP2040_IOBANK0_IRQ_BANKS];
};

bool rp2040_iobank0_uart0_tx_enabled(RP2040IoBank0State *s);
bool rp2040_iobank0_uart0_rx_enabled(RP2040IoBank0State *s);
bool rp2040_iobank0_uart1_tx_enabled(RP2040IoBank0State *s);
bool rp2040_iobank0_uart1_rx_enabled(RP2040IoBank0State *s);

#endif
