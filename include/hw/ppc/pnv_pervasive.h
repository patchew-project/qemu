/*
 * QEMU PowerPC pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_PERVASIVE_H
#define PPC_PNV_PERVASIVE_H


typedef struct PnvChipletControlRegs {

    uint64_t cplt_ctrl[6];
    uint64_t cplt_cfg0;
    uint64_t cplt_cfg1;
    uint64_t cplt_stat0;
    uint64_t cplt_mask0;
    uint64_t ctrl_protect_mode;
    uint64_t ctrl_atomic_lock;

} PnvChipletControlRegs;

uint64_t pnv_chiplet_ctrl_read(PnvChipletControlRegs *ctrl_regs, hwaddr reg,
                unsigned size);
void pnv_chiplet_ctrl_write(PnvChipletControlRegs *ctrl_regs, hwaddr reg,
                uint64_t val, unsigned size);
#endif /*PPC_PNV_PERVASIVE_H */
