/*
 * Synopsys DesignWare SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Emulates the DesignWare SSI controller in Standard SPI mode,
 * covering the PIO/FIFO data path and chip selects.
 */

#ifndef HW_SSI_DW_SSI_H
#define HW_SSI_DW_SSI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo32.h"
#include "qom/object.h"

#define TYPE_DW_SSI "designware-ssi"
OBJECT_DECLARE_SIMPLE_TYPE(DwSsiState, DW_SSI)

#define DW_SSI_MMIO_SIZE 0x1000
#define DW_SSI_REGS_SIZE 0x14c
#define DW_SSI_NUM_REGS \
    (DW_SSI_REGS_SIZE / sizeof(uint32_t))

typedef enum DwSsiPhase {
    DW_SSI_PHASE_IDLE,
    DW_SSI_PHASE_STANDARD_TX_ONLY,
    DW_SSI_PHASE_RX_ONLY,
    DW_SSI_PHASE_EEPROM_COMMAND,
    DW_SSI_PHASE_EEPROM_DATA,
    DW_SSI_PHASE_STANDARD_TR,
} DwSsiPhase;

typedef struct DwSsiConfig {
    uint32_t num_cs;
    uint32_t fifo_depth;
    uint32_t imr_reset;
} DwSsiConfig;

struct DwSsiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    SSIBus *spi;

    qemu_irq *cs_lines;

    Fifo32 tx_fifo;
    Fifo32 rx_fifo;
    uint32_t regs[DW_SSI_NUM_REGS];

    DwSsiConfig cfg;

    uint32_t phase;
    uint32_t remaining_frames;

    int active_cs;
};
#endif /* HW_SSI_DW_SSI_H */
