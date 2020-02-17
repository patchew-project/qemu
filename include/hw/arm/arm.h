/*
 * Copyright (c) 2015 Linaro Limited
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
 * this program.  If not, see <http://www.gnu.org/licenses/>.*
 */

#ifndef QEMU_ARM_ARM_H
#define QEMU_ARM_ARM_H

#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gicv3_common.h"

#define NUM_GICV2M_SPIS       64
#define NUM_VIRTIO_TRANSPORTS 32
#define NUM_SMMU_IRQS          4

#define ARCH_GIC_MAINT_IRQ  9

#define ARCH_TIMER_VIRT_IRQ   11
#define ARCH_TIMER_S_EL1_IRQ  13
#define ARCH_TIMER_NS_EL1_IRQ 14
#define ARCH_TIMER_NS_EL2_IRQ 10

#define VIRTUAL_PMU_IRQ 7

#define PPI(irq) ((irq) + 16)


enum {
    VIRT_FLASH,
    VIRT_MEM,
    VIRT_CPUPERIPHS,
    VIRT_GIC_DIST,
    VIRT_GIC_CPU,
    VIRT_GIC_V2M,
    VIRT_GIC_HYP,
    VIRT_GIC_VCPU,
    VIRT_GIC_ITS,
    VIRT_GIC_REDIST,
    VIRT_SMMU,
    VIRT_UART,
    VIRT_MMIO,
    VIRT_RTC,
    VIRT_FW_CFG,
    VIRT_PCIE,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_PIO,
    VIRT_PCIE_ECAM,
    VIRT_PLATFORM_BUS,
    VIRT_GPIO,
    VIRT_SECURE_UART,
    VIRT_SECURE_MEM,
    VIRT_PCDIMM_ACPI,
    VIRT_ACPI_GED,
    VIRT_LOWMEMMAP_LAST,
};

/* indices of IO regions located after the RAM */
enum {
    VIRT_HIGH_GIC_REDIST2 =  VIRT_LOWMEMMAP_LAST,
    VIRT_HIGH_PCIE_ECAM,
    VIRT_HIGH_PCIE_MMIO,
};

/* Number of external interrupt lines to configure the GIC with */
#define NUM_IRQS 256

/* indices of IO regions located after the RAM */

typedef struct MemMapEntry {
    hwaddr base;
    hwaddr size;
} MemMapEntry;

typedef struct {
    MachineClass parent;
    bool claim_edge_triggered_timers;
} ArmMachineClass;

typedef struct {
    MachineState parent;
    int32_t gic_version;
    MemMapEntry *memmap;
    const int *irqmap;
    int smp_cpus;
    void *fdt;
    int fdt_size;
    uint32_t clock_phandle;
    uint32_t gic_phandle;
    int psci_conduit;
    DeviceState *gic;
} ArmMachineState;

#define TYPE_ARM_MACHINE   MACHINE_TYPE_NAME("arm")
#define ARM_MACHINE(obj) \
    OBJECT_CHECK(ArmMachineState, (obj), TYPE_ARM_MACHINE)
#define ARM_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ArmMachineClass, obj, TYPE_ARM_MACHINE)
#define ARM_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(ArmMachineClass, klass, TYPE_ARM_MACHINE)

void qdev_create_gic(ArmMachineState *ams);

void init_gic_sysbus(ArmMachineState *ams);

void create_uart(const ArmMachineState *ams, int uart,
                 MemoryRegion *mem, Chardev *chr);

void create_rtc(const ArmMachineState *ams);

void create_virtio_devices(const ArmMachineState *ams);

/* Return the number of used redistributor regions  */
static inline int virt_gicv3_redist_region_count(ArmMachineState *ams)
{
    uint32_t redist0_capacity =
                ams->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;

    assert(ams->gic_version == 3);

    return ams->smp_cpus > redist0_capacity ? 2 : 1;
}

#endif /* QEMU_ARM_ARM_H */
