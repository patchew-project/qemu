/*
 * RP2040 XIP/SSI flash controller emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_RP2040_XIP_H
#define HW_SSI_RP2040_XIP_H

#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qom/object.h"

#define TYPE_RP2040_XIP "rp2040-xip"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040XipState, RP2040_XIP)

#define RP2040_XIP_CTRL_BASE 0x14000000
#define RP2040_XIP_SSI_BASE  0x18000000
#define RP2040_XIP_AUX_BASE  0x50400000
#define RP2040_XIP_CTRL_SIZE 0x4000
#define RP2040_XIP_SSI_SIZE  0x4000
#define RP2040_XIP_AUX_SIZE  0x4000
#define RP2040_XIP_STREAM_FIFO_DEPTH 4

struct RP2040XipState {
    SysBusDevice parent_obj;

    MemoryRegion xip;
    MemoryRegion xip_noalloc;
    MemoryRegion xip_nocache;
    MemoryRegion xip_nocache_noalloc;
    MemoryRegion ctrl;
    MemoryRegion ssi;
    MemoryRegion aux;
    qemu_irq dreq_rx;
    qemu_irq dreq_stream;

    uint32_t flash_size;
    char *flash_file;
    uint64_t flash_uid;
    uint8_t *storage;
    bool xip_writable;

    uint32_t xip_ctrl;

    uint32_t ctrlr0;
    uint32_t ctrlr1;
    uint32_t ssienr;
    uint32_t ser;
    uint32_t baudr;
    uint32_t txftlr;
    uint32_t rxftlr;
    uint32_t imr;
    uint32_t dmacr;
    uint32_t dmatdlr;
    uint32_t dmardlr;
    uint32_t rx_sample_dly;
    uint32_t spi_ctrlr0;

    bool write_enable;
    bool busy;
    bool synthetic_hardfault_vector_enabled;
    uint32_t synthetic_hardfault_vector;
    bool qspi_cs_high;
    uint8_t tx[260];
    unsigned tx_len;
    bool tx_unsupported_logged;
    uint8_t rx[16];
    unsigned rx_len;
    unsigned rx_pos;
    uint32_t ssi_bulk_addr;
    uint32_t ssi_bulk_remaining;

    uint32_t stream_addr;
    uint32_t stream_ctr;
    uint32_t stream_fifo[RP2040_XIP_STREAM_FIFO_DEPTH];
    unsigned stream_fifo_len;
    unsigned stream_fifo_pos;
};

void rp2040_xip_set_writable(RP2040XipState *s, bool writable);
void rp2040_xip_set_synthetic_hardfault_vector(RP2040XipState *s,
                                               uint32_t handler);
void rp2040_xip_load_image(RP2040XipState *s, const char *filename,
                           Error **errp);
void rp2040_xip_qspi_cs(RP2040XipState *s, bool high);
bool rp2040_xip_flash_range_erase(RP2040XipState *s, uint32_t flash_offs,
                                  uint32_t count, uint32_t block_size,
                                  uint8_t block_cmd, Error **errp);
bool rp2040_xip_flash_range_program(RP2040XipState *s, uint32_t flash_offs,
                                    uint32_t data_addr, uint32_t count,
                                    Error **errp);

#endif
