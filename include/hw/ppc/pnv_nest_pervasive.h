/*
 * QEMU PowerPC nest pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_NEST_PERVASIVE_H
#define PPC_PNV_NEST_PERVASIVE_H

#define TYPE_PNV_NEST_PERVASIVE "pnv-nest-chiplet-pervasive"
#define PNV_NEST_PERVASIVE(obj) OBJECT_CHECK(PnvNestChipletPervasive, (obj), TYPE_PNV_NEST_PERVASIVE)

typedef struct PnvPervasiveCtrlRegs {
#define CPLT_CTRL_SIZE 6
    uint64_t cplt_ctrl[CPLT_CTRL_SIZE];
    uint64_t cplt_cfg0;
    uint64_t cplt_cfg1;
    uint64_t cplt_stat0;
    uint64_t cplt_mask0;
    uint64_t ctrl_protect_mode;
    uint64_t ctrl_atomic_lock;
} PnvPervasiveCtrlRegs;

typedef struct PnvNestChipletPervasive {
    DeviceState             parent;
    char                    *parent_obj_name;
    MemoryRegion            xscom_ctrl_regs;
    PnvPervasiveCtrlRegs    control_regs;
} PnvNestChipletPervasive;

#endif /*PPC_PNV_NEST_PERVASIVE_H */
