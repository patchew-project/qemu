/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef ARMR52VIRT_H
#define ARMR52VIRT_H

#include "hw/arm/boot.h"
#include "hw/intc/arm_gic.h"
#include "hw/char/pl011.h"
#include "include/exec/address-spaces.h"
#include "hw/cpu/cluster.h"
#include "target/arm/cpu.h"
#include "qom/object.h"
#include "hw/intc/arm_gicv3_common.h"

#define TYPE_ARMR52VIRT "armr52virt"
OBJECT_DECLARE_SIMPLE_TYPE(ArmR52VirtState, ARMR52VIRT)

#define ARMR52_VIRT_NUM_APU_CPUS 4
#define ARMR52_VIRT_NUM_UARTS 1
#define ARMR52_VIRT_GIC_REGIONS 6

#define ARCH_TIMER_VIRT_IRQ   11
#define ARCH_TIMER_S_EL1_IRQ  13
#define ARCH_TIMER_NS_EL1_IRQ 14
#define ARCH_TIMER_NS_EL2_IRQ 10
#define NUM_IRQS 256

struct ArmR52VirtState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CPUClusterState apu_cluster;
    ARMCPU apu_cpu[ARMR52_VIRT_NUM_APU_CPUS];
    GICv3State gic;

    MemoryRegion ddr_ram;

    PL011State uart[ARMR52_VIRT_NUM_UARTS];

    char *boot_cpu;
    ARMCPU *boot_cpu_ptr;

    /* Has the ARM Security extensions?  */
    bool secure;
    /* Has the ARM Virtualization extensions?  */
    bool virt;

};

#endif
