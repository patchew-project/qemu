/*
 * STM32L4x5 QuadSPI controller
 *
 * Copyright (c) 2026 EXALT Technologies
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_STM32L4X5_QUADSPI_H
#define HW_STM32L4X5_QUADSPI_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_QUADSPI "stm32l4x5-quadspi"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l4x5QuadSpiState, STM32L4X5_QUADSPI)

struct Stm32l4x5QuadSpiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    Clock *clk;
    qemu_irq irq;
    qemu_irq cs;
    SSIBus *ssi;

    uint32_t cr;
    uint32_t dcr;
    uint32_t sr;
    uint32_t dlr;
    uint32_t ccr;
    uint32_t ar;
    uint32_t abr;
    uint32_t dr;
    uint32_t psmkr;
    uint32_t psmar;
    uint32_t pir;
    uint32_t lptr;

    Fifo8 fifo;
    uint64_t remaining;
    bool transaction_active;
    bool rx_stalled;
    bool indefinite;
    bool write_pending;
};

#endif /* HW_STM32L4X5_QUADSPI_H */
