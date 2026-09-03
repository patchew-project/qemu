/*
 * Synopsys DesignWare Multimedia Card Interface
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SD_DW_MCI_H
#define HW_SD_DW_MCI_H

#include "hw/core/register.h"
#include "hw/core/sysbus.h"
#include "hw/sd/sd.h"
#include "qom/object.h"

#define TYPE_DW_MCI "dw-mci"
OBJECT_DECLARE_TYPE(DwMciState, DwMciClass, DW_MCI)

#define DW_MCI_MMIO_SIZE      0x1000
#define DW_MCI_REG_COUNT      (DW_MCI_MMIO_SIZE / sizeof(uint32_t))
#define DW_MCI_FIFO_MAX_WORDS 2048
#define DW_MCI_FIFO_MAX_BYTES (DW_MCI_FIFO_MAX_WORDS * sizeof(uint32_t))

struct DwMciState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SDBus sdbus;
    qemu_irq irq;

    uint32_t regs[DW_MCI_REG_COUNT];
    RegisterInfo regs_info[DW_MCI_REG_COUNT];

    uint8_t fifo[DW_MCI_FIFO_MAX_BYTES];
    uint32_t fifo_head;
    uint32_t fifo_len;
    uint32_t transfer_remaining;
    uint64_t idmac_desc_addr;
    bool transfer_active;
    bool transfer_write;
    bool transfer_send_stop;
    bool idmac_suspended;
    bool idmac_fatal;
    bool card_inserted;
    bool card_readonly;

    uint32_t verid;
    uint32_t hcon;
    uint32_t data_offset;
    uint32_t fifo_depth;
};

struct DwMciClass {
    SysBusDeviceClass parent_class;

    uint32_t verid;
    uint32_t hcon;
    uint32_t data_offset;
    uint32_t fifo_depth;

    bool (*vendor_read)(DwMciState *s, hwaddr offset, uint64_t *value,
                        unsigned size);
    bool (*vendor_write)(DwMciState *s, hwaddr offset, uint64_t value,
                         unsigned size);
    void (*vendor_reset)(DwMciState *s);
};

SDBus *dw_mci_get_bus(DwMciState *s);

#endif
