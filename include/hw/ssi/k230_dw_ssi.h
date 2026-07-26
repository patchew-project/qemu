/*
 * Kendryte K230 DesignWare SSI
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Emulates the DesignWare SSI controllers documented by the K230
 * Technical Reference Manual, including standard SPI, Dual/Quad SDR,
 * internal DMA, and the XIP read window.
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */

#ifndef HW_SSI_K230_DW_SSI_H
#define HW_SSI_K230_DW_SSI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo32.h"
#include "qom/object.h"

#define TYPE_K230_DW_SSI "riscv.k230.dw-ssi"
OBJECT_DECLARE_SIMPLE_TYPE(K230DwSsiState, K230_DW_SSI)

#define K230_DW_SSI_MMIO_SIZE 0x1000
#define K230_DW_SSI_REGS_SIZE 0x14c
#define K230_DW_SSI_NUM_REGS \
    (K230_DW_SSI_REGS_SIZE / sizeof(uint32_t))

struct K230DwSsiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    SSIBus *spi;
    qemu_irq *cs_lines;

    Fifo32 tx_fifo;
    Fifo32 rx_fifo;
    uint32_t regs[K230_DW_SSI_NUM_REGS];

    uint32_t num_cs;
    uint32_t max_lines;
    int active_cs;
};

#endif /* HW_SSI_K230_DW_SSI_H */
