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

#define TYPE_RISCV_WGCHECKER  "riscv.wgchecker"

typedef struct RISCVWgCheckerState RISCVWgCheckerState;
DECLARE_INSTANCE_CHECKER(RISCVWgCheckerState, RISCV_WGCHECKER,
                         TYPE_RISCV_WGCHECKER)

#define TYPE_RISCV_WGC_IOMMU_MEMORY_REGION    "riscv-wgc-iommu-memory-region"

typedef struct WgCheckerSlot WgCheckerSlot;
struct WgCheckerSlot {
    uint64_t addr;
    uint64_t perm;
    uint32_t cfg;
};

typedef struct WgCheckerRegion WgCheckerRegion;
struct WgCheckerRegion {
    MemoryRegion *downstream;
    uint64_t region_offset;

    IOMMUMemoryRegion upstream;
    MemoryRegion blocked_io;
    AddressSpace downstream_as;
    AddressSpace blocked_io_as;

    RISCVWgCheckerState *wgchecker;
};

#define WGC_NUM_REGIONS     64

struct RISCVWgCheckerState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq irq;

    /* error reg */
    uint64_t errcause;
    uint64_t erraddr;

    /* Memory regions protected by wgChecker */
    WgCheckerRegion mem_regions[WGC_NUM_REGIONS];

    /* Property */
    uint32_t slot_count; /* nslots */
    uint32_t mmio_size;
    uint64_t addr_range_start;
    uint64_t addr_range_size;
    bool hw_bypass;
};

DeviceState *riscv_wgchecker_create(hwaddr addr, uint32_t size,
                                    qemu_irq irq, uint32_t slot_count,
                                    uint64_t addr_range_start,
                                    uint64_t addr_range_size,
                                    uint32_t num_of_region,
                                    MemoryRegion **downstream,
                                    uint64_t *region_offset,
                                    uint32_t num_default_slots,
                                    WgCheckerSlot *default_slots);

#endif
