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
#endif /*PPC_PNV_PERVASIVE_H */
