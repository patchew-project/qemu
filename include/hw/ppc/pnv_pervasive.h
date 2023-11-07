/*
 * QEMU PowerPC pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_PERVASIVE_H
#define PPC_PNV_PERVASIVE_H

#define TYPE_PNV_PERV_CHIPLET "pnv-pervasive-chiplet"
#define PNV_PERVCHIPLET(obj) OBJECT_CHECK(PnvPervChiplet, (obj), TYPE_PNV_PERV_CHIPLET)

typedef struct ControlRegs {

    uint64_t cplt_ctrl[6];
    uint64_t cplt_cfg0;
    uint64_t cplt_cfg1;
    uint64_t cplt_stat0;
    uint64_t cplt_mask0;
    uint64_t ctrl_protect_mode;
    uint64_t ctrl_atomic_lock;
} ControlRegs;

typedef struct PnvPervChiplet {

    DeviceState parent;
    struct PnvChip *chip;
    MemoryRegion xscom_perv_ctrl_regs;
    ControlRegs control_regs;

} PnvPervChiplet;

void pnv_perv_dt(uint32_t base, void *fdt, int offset);
#endif /*PPC_PNV_PERVASIVE_H */
