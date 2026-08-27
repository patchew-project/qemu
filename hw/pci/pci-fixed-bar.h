/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * Fixed-BAR — QEMU side declarations.
 *
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_PCI_FIXED_BAR_H
#define HW_PCI_FIXED_BAR_H

#include "hw/nvram/fw_cfg.h"
#include "hw/pci/pci_bus.h"

#define FW_CFG_FIXED_BARS        "etc/fixed-bars"
#define QEMU_FIXED_BARS_VERSION  1

/*
 * On-wire layout (little-endian, packed):
 *
 *   QemuFixedBarsHdr
 *
 *   For each device (num_devices total):
 *     QemuFixedBarsDevice               -- device header
 *     QemuFixedBarsBar[num_bars]        -- one record per BAR (FIXED only)
 */

typedef struct {
    uint32_t version;     /* QEMU_FIXED_BARS_VERSION */
    uint32_t num_devices;
} QEMU_PACKED QemuFixedBarsHdr;  /* 8 bytes */

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  dev_flags;  /* QEMU_FIXED_BARS_DEV_F_* */
    uint8_t  rp_bus;     /* primary bus of the root port this device is under */
    uint8_t  num_bars;
    uint8_t  reserved;
} QEMU_PACKED QemuFixedBarsDevice;  /* 8 bytes */

typedef struct {
    uint8_t  bar;
    uint8_t  reserved[3];
    uint32_t flags;    /* QEMU_FIXED_BAR_F_* */
    uint64_t address;
    uint64_t size;
} QEMU_PACKED QemuFixedBarsBar;  /* 24 bytes */

#define QEMU_FIXED_BAR_F_MEM64       (1u << 0)
#define QEMU_FIXED_BAR_F_PREF        (1u << 1)
#define QEMU_FIXED_BARS_DEV_F_FIXED  (1u << 0)

bool fixed_bars_write_blob(FWCfgState *fw_cfg,
                           uint64_t mmio32_base,
                           uint64_t mmio32_size,
                           uint64_t mmio64_base,
                           uint64_t mmio64_size);

#endif /* HW_PCI_FIXED_BAR_H */
