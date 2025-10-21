/*
 * RISC-V WorldGuard Devices
 *
 * Copyright (c) 2022 RISCV, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_WORLDGUARD_H
#define HW_RISCV_WORLDGUARD_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "exec/hwaddr.h"

#define TYPE_RISCV_WORLDGUARD "riscv.worldguard"

#define NO_TRUSTEDWID           UINT32_MAX

typedef struct RISCVWorldGuardState RISCVWorldGuardState;
DECLARE_INSTANCE_CHECKER(RISCVWorldGuardState, RISCV_WORLDGUARD,
                         TYPE_RISCV_WORLDGUARD)

struct RISCVWorldGuardState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/

    /* Property */
    uint32_t nworlds;
    uint32_t trustedwid;
    bool hw_bypass;
    bool tz_compat;
};

extern struct RISCVWorldGuardState *worldguard_config;

DeviceState *riscv_worldguard_create(uint32_t nworlds, uint32_t trustedwid,
                                     bool hw_bypass, bool tz_compat);
void riscv_worldguard_apply_cpu(uint32_t hartid);

void wid_to_mem_attrs(MemTxAttrs *attrs, uint32_t wid);
uint32_t mem_attrs_to_wid(MemTxAttrs attrs);
bool could_access_wgblocks(MemTxAttrs attrs, const char *wgblock);

#endif
