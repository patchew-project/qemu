/*
 * Spacemit K3 SPI Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#ifndef HW_SSI_SPACEMIT_K3_SPI_H
#define HW_SSI_SPACEMIT_K3_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_SPACEMIT_K3_SPI "spacemit-k3-spi"
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3SpiState, SPACEMIT_K3_SPI)

#define SPACEMIT_K3_SPI_TXFIFO_SIZE 256
#define SPACEMIT_K3_SPI_RXFIFO_SIZE 128

struct SpacemitK3SpiState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    MemoryRegion ahb;
    SSIBus *spi;
    qemu_irq irq;
    qemu_irq cs_line;

    uint32_t mcr;
    uint32_t ipcr;
    uint32_t sfar;
    uint32_t fr;

    uint32_t lut1[4];

    uint8_t rxbuf[SPACEMIT_K3_SPI_RXFIFO_SIZE];
    uint32_t rxlevel;

    uint8_t txbuf[SPACEMIT_K3_SPI_TXFIFO_SIZE];
    uint32_t txlevel;

    uint32_t ahb_base;
};

#endif /* HW_SSI_SPACEMIT_K3_SPI_H */
