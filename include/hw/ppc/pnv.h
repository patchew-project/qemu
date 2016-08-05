/*
 * QEMU PowerNV various definitions
 *
 * Copyright (c) 2014-2016 BenH, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _PPC_PNV_H
#define _PPC_PNV_H

#include "hw/boards.h"
#include "hw/sysbus.h"

#define TYPE_PNV_CHIP "powernv-chip"
#define PNV_CHIP(obj) OBJECT_CHECK(PnvChip, (obj), TYPE_PNV_CHIP)

typedef struct PnvChip {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    uint32_t     chip_id;
    uint32_t     num_cores;
    char *cpu_model;

    Object **cores;
} PnvChip;

#define TYPE_POWERNV_MACHINE      "powernv-machine"
#define POWERNV_MACHINE(obj) \
    OBJECT_CHECK(sPowerNVMachineState, (obj), TYPE_POWERNV_MACHINE)

typedef struct sPowerNVMachineState {
    /*< private >*/
    MachineState parent_obj;

    uint32_t initrd_base;
    long initrd_size;
    hwaddr fdt_addr;

    uint32_t  num_chips;
    PnvChip   *chips;
} sPowerNVMachineState;

#define PNV_TIMEBASE_FREQ           512000000ULL

#endif /* _PPC_PNV_H */
