/*
 * Synopsys DesignWare SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Emulates the DesignWare SSI controller in Standard SPI mode,
 * covering the PIO/FIFO data path, interrupt outputs and cs.
 */

#ifndef HW_SSI_DWC_SSI_H
#define HW_SSI_DWC_SSI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo32.h"
#include "qom/object.h"

#define TYPE_DWC_SSI "dwc-ssi"
OBJECT_DECLARE_SIMPLE_TYPE(DwcSsiState, DWC_SSI)

#define DWC_SSI_MMIO_SIZE 0x1000
#define DWC_SSI_REGS_SIZE 0x14c
#define DWC_SSI_NUM_REGS \
    (DWC_SSI_REGS_SIZE / sizeof(uint32_t))

/* SSI GPIO output ordering differs from RISR/ISR bit ordering */
typedef enum DwcSsiIrq {
    DWC_SSI_IRQ_TXE,
    DWC_SSI_IRQ_TXO,
    DWC_SSI_IRQ_RXF,
    DWC_SSI_IRQ_RXO,
    DWC_SSI_IRQ_TXU,
    DWC_SSI_IRQ_RXU,
    DWC_SSI_IRQ_MST,
    DWC_SSI_IRQ_DONE,
    DWC_SSI_IRQ_AXIE,
    DWC_SSI_IRQ_COUNT,
} DwcSsiIrq;

typedef enum DwcSsiPhase {
    DWC_SSI_PHASE_IDLE,
    DWC_SSI_PHASE_STANDARD_TX_ONLY,
    DWC_SSI_PHASE_RX_ONLY,
    DWC_SSI_PHASE_EEPROM_COMMAND,
    DWC_SSI_PHASE_EEPROM_DATA,
    DWC_SSI_PHASE_STANDARD_TR,
    DWC_SSI_PHASE_COUNT,
} DwcSsiPhase;

typedef struct DwcSsiConfig {
    uint32_t num_cs;
    uint32_t fifo_depth;
    bool master_mode;
} DwcSsiConfig;

struct DwcSsiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    SSIBus *spi;

    qemu_irq *cs_lines;
    qemu_irq irqs[DWC_SSI_IRQ_COUNT];

    Fifo32 tx_fifo;
    Fifo32 rx_fifo;
    uint32_t regs[DWC_SSI_NUM_REGS];

    DwcSsiConfig cfg;

    uint32_t irq_latched;
    uint32_t phase;
    uint32_t remaining_frames;
    uint32_t dummy_frame;

    int active_cs;
};
#endif /* HW_SSI_DWC_SSI_H */
