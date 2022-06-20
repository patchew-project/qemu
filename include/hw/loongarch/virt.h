/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for loongarch board emulation.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_H
#define HW_LOONGARCH_H

#include "target/loongarch/cpu.h"
#include "hw/boards.h"
#include "qemu/queue.h"
#include "hw/intc/loongarch_ipi.h"

#define LOONGARCH_MAX_VCPUS     4

#define LOONGARCH_ISA_IO_BASE   0x18000000UL
#define LOONGARCH_ISA_IO_SIZE   0x0004000
#define FW_CFG_ADDR             0x1e020000
#define LA_BIOS_BASE            0x1c000000
#define LA_BIOS_SIZE            (4 * MiB)

#define LOW_MEM_BASE 0
#define LOW_MEM_SIZE 0x10000000
#define HIGH_MEM_BASE 0x90000000
#define GED_EVT_ADDR 0x100e0000
#define GED_MEM_ADDR (GED_EVT_ADDR + ACPI_GED_EVT_SEL_LEN)
#define GED_REG_ADDR (GED_MEM_ADDR + MEMORY_HOTPLUG_IO_LEN)

struct LoongArchMachineState {
    /*< private >*/
    MachineState parent_obj;

    IPICore ipi_core[MAX_IPI_CORE_NUM];
    MemoryRegion lowmem;
    MemoryRegion highmem;
    MemoryRegion isa_io;
    MemoryRegion bios;

    /* State for other subsystems/APIs: */
    FWCfgState  *fw_cfg;
    Notifier machine_done;
    OnOffAuto   acpi;
    char        *oem_id;
    char        *oem_table_id;
    DeviceState *acpi_ged;
};

#define TYPE_LOONGARCH_MACHINE  MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchMachineState, LOONGARCH_MACHINE)
bool loongarch_is_acpi_enabled(LoongArchMachineState *lams);
void loongarch_acpi_setup(LoongArchMachineState *lams);
#endif
