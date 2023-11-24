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

#define TYPE_PNV_PERV "pnv-pervasive-chiplet"
#define PNV_PERV(obj) OBJECT_CHECK(PnvPerv, (obj), TYPE_PNV_PERV)

typedef struct PnvPervCtrlRegs {
#define CPLT_CTRL_SIZE 6
    uint64_t cplt_ctrl[CPLT_CTRL_SIZE];
    uint64_t cplt_cfg0;
    uint64_t cplt_cfg1;
    uint64_t cplt_stat0;
    uint64_t cplt_mask0;
    uint64_t ctrl_protect_mode;
    uint64_t ctrl_atomic_lock;
} PnvPervCtrlRegs;

typedef struct PnvPerv {
    DeviceState parent;
    char *parent_obj_name;
    MemoryRegion xscom_perv_ctrl_regs;
    PnvPervCtrlRegs control_regs;
} PnvPerv;

void pnv_perv_dt(PnvPerv *perv, uint32_t base_addr, void *fdt, int offset);
#endif /*PPC_PNV_PERVASIVE_H */
