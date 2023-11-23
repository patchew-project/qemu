/*
 * QEMU PowerPC PowerNV Emulation of some CHIPTOD behaviour
 *
 * Copyright (c) 2022-2023, IBM Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef PPC_PNV_CHIPTOD_H
#define PPC_PNV_CHIPTOD_H

#include "qom/object.h"

#define TYPE_PNV_CHIPTOD "pnv-chiptod"
OBJECT_DECLARE_TYPE(PnvChipTOD, PnvChipTODClass, PNV_CHIPTOD)
#define TYPE_PNV9_CHIPTOD TYPE_PNV_CHIPTOD "-POWER9"
DECLARE_INSTANCE_CHECKER(PnvChipTOD, PNV9_CHIPTOD, TYPE_PNV9_CHIPTOD)
#define TYPE_PNV10_CHIPTOD TYPE_PNV_CHIPTOD "-POWER10"
DECLARE_INSTANCE_CHECKER(PnvChipTOD, PNV10_CHIPTOD, TYPE_PNV10_CHIPTOD)

enum tod_state {
    tod_error = 0,
    tod_not_set = 7,
    tod_not_set_step = 11,
    tod_running = 2,
    tod_running_step = 10,
    tod_running_sync = 14,
    tod_wait_for_sync = 13,
    tod_stopped = 1,
};

typedef struct PnvCore PnvCore;

struct PnvChipTOD {
    DeviceState xd;

    PnvChip *chip;
    MemoryRegion xscom_regs;

    bool primary;
    bool secondary;
    enum tod_state tod_state;
    uint64_t tod_error;
    uint64_t pss_mss_ctrl_reg;
    PnvCore *slave_pc_target;
};

struct PnvChipTODClass {
    DeviceClass parent_class;

    int xscom_size;
    const MemoryRegionOps *xscom_ops;
};

#endif /* PPC_PNV_CHIPTOD_H */
