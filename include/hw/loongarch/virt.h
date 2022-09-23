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

enum {
    VIRT_LOWDDR,
    VIRT_PCH,
    VIRT_PM,
    VIRT_RTC,
    VIRT_ACPI_GED,
    VIRT_ISA_IO,
    VIRT_PCI_IO,
    VIRT_BIOS,
    VIRT_FDT,
    VIRT_FW_CFG,
    VIRT_UART,
    VIRT_PCI_CFG,
    VIRT_MSI,
    VIRT_PCI_MEM,
    VIRT_HIGHDDR,
    VIRT_PLATFORM_BUS,
};

struct LoongArchMachineState {
    /*< private >*/
    MachineState parent_obj;

    IPICore ipi_core[MAX_IPI_CORE_NUM];
    MemoryRegion lowmem;
    MemoryRegion highmem;
    MemoryRegion isa_io;
    MemoryRegion bios;
    bool         bios_loaded;
    /* State for other subsystems/APIs: */
    FWCfgState  *fw_cfg;
    Notifier     machine_done;
    OnOffAuto    acpi;
    char         *oem_id;
    char         *oem_table_id;
    DeviceState  *acpi_ged;
    int          fdt_size;
    DeviceState *platform_bus_dev;
    PCIBus       *pci_bus;
    MemMapEntry  *memmap;
};

#define TYPE_LOONGARCH_MACHINE  MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchMachineState, LOONGARCH_MACHINE)
bool loongarch_is_acpi_enabled(LoongArchMachineState *lams);
void loongarch_acpi_setup(LoongArchMachineState *lams);
#endif
