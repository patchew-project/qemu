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

/* SSI GPIO output ordering differs from RISR/ISR bit ordering. */
typedef enum K230DwSsiIrq {
    K230_DW_SSI_IRQ_TXE,
    K230_DW_SSI_IRQ_TXO,
    K230_DW_SSI_IRQ_RXF,
    K230_DW_SSI_IRQ_RXO,
    K230_DW_SSI_IRQ_TXU,
    K230_DW_SSI_IRQ_RXU,
    K230_DW_SSI_IRQ_MST,
    K230_DW_SSI_IRQ_DONE,
    K230_DW_SSI_IRQ_AXIE,
    K230_DW_SSI_IRQ_COUNT,
} K230DwSsiIrq;

typedef enum K230DwSsiPhase {
    K230_DW_SSI_PHASE_IDLE,
    K230_DW_SSI_PHASE_STANDARD_TX_ONLY,
    K230_DW_SSI_PHASE_RX_ONLY,
    K230_DW_SSI_PHASE_EEPROM_COMMAND,
    K230_DW_SSI_PHASE_EEPROM_DATA,

    K230_DW_SSI_PHASE_ENHANCED_INSTRUCTION,
    K230_DW_SSI_PHASE_ENHANCED_ADDRESS,
    K230_DW_SSI_PHASE_ENHANCED_MODE,
    K230_DW_SSI_PHASE_ENHANCED_DUMMY,
    K230_DW_SSI_PHASE_ENHANCED_DATA,
} K230DwSsiPhase;

typedef struct K230DwSsiEnhancedCommand {
    uint32_t instruction;
    uint32_t address;
    uint32_t mode;
    uint32_t instruction_bits;
    uint32_t address_bits;
    uint32_t mode_bits;
    uint32_t wait_cycles;
    uint32_t data_frames;
    uint32_t spi_frf;
    uint32_t trans_type;
    uint32_t tmod;
    bool mode_bits_enabled;
} K230DwSsiEnhancedCommand;

struct K230DwSsiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    SSIBus *spi;
    qemu_irq *cs_lines;
    qemu_irq irqs[K230_DW_SSI_IRQ_COUNT];

    Fifo32 tx_fifo;
    Fifo32 rx_fifo;
    uint32_t regs[K230_DW_SSI_NUM_REGS];

    uint32_t irq_latched;

    uint32_t phase;
    uint32_t remaining_frames;
    K230DwSsiEnhancedCommand enhanced;

    uint32_t num_cs;
    uint32_t max_lines;
    int active_cs;
};

#endif /* HW_SSI_K230_DW_SSI_H */
