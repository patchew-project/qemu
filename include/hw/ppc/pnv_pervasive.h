/*
 * QEMU PowerPC pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
