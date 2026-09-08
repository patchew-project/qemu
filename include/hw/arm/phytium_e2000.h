/*
 * Phytium E2000 SoC model
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_PHYTIUM_E2000_H
#define HW_ARM_PHYTIUM_E2000_H

#include "hw/arm/boot.h"
#include "hw/core/sysbus.h"
#include "hw/net/cadence_gem.h"
#include "hw/sd/phytium_e2000_mci.h"
#include "qemu/typedefs.h"
#include "target/arm/cpu.h"

#define TYPE_PHYTIUM_E2000_SOC "phytium-e2000-soc"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000SoCState, PHYTIUM_E2000_SOC)

#define PHYTIUM_E2000_NUM_CPUS 4
#define PHYTIUM_E2000_NUM_MCIS 2
#define PHYTIUM_E2000_NUM_GEMS 4

enum {
    PHYTIUM_E2000_LOW_PERIPH,
    PHYTIUM_E2000_MCI0,
    PHYTIUM_E2000_MCI1,
    PHYTIUM_E2000_UART0,
    PHYTIUM_E2000_UART1,
    PHYTIUM_E2000_UART2,
    PHYTIUM_E2000_UART3,
    PHYTIUM_E2000_UART4,
    PHYTIUM_E2000_UART5,
    PHYTIUM_E2000_UART6,
    PHYTIUM_E2000_GIC_DIST,
    PHYTIUM_E2000_GIC_ITS,
    PHYTIUM_E2000_GIC_REDIST,
    PHYTIUM_E2000_BOOT_SRAM,
    PHYTIUM_E2000_PCIE_CTRL,
    PHYTIUM_E2000_PCIE_PHY_CTRL,
    PHYTIUM_E2000_BOARD_CTRL,
    PHYTIUM_E2000_GEM0,
    PHYTIUM_E2000_GEM1,
    PHYTIUM_E2000_GEM2,
    PHYTIUM_E2000_GEM3,
    PHYTIUM_E2000_BOOT_IACC,
    PHYTIUM_E2000_PCIE_ECAM,
    PHYTIUM_E2000_PCIE_PIO,
    PHYTIUM_E2000_PCIE_MMIO,
    PHYTIUM_E2000_RAM,
    PHYTIUM_E2000_PCIE_MMIO_HIGH,
    PHYTIUM_E2000_RAM_HIGH,
};

extern const MemMapEntry phytium_e2000_memmap[];

struct PhytiumE2000SoCState {
    SysBusDevice parent_obj;

    DeviceState *gic;
    PhytiumE2000MciState *mci[PHYTIUM_E2000_NUM_MCIS];
    CadenceGEMState *gem[PHYTIUM_E2000_NUM_GEMS];
    ARMCPU cpu[PHYTIUM_E2000_NUM_CPUS];
    unsigned int num_cpus;
};

void phytium_e2000_soc_configure(PhytiumE2000SoCState *s,
                                 unsigned int num_cpus);
void phytium_e2000_soc_cpu_topology(unsigned int index,
                                    uint64_t *mp_affinity,
                                    int64_t *cluster_id,
                                    int64_t *core_id);
ARMCPU *phytium_e2000_soc_cpu(PhytiumE2000SoCState *s,
                              unsigned int index);
void phytium_e2000_soc_attach_sd_card(PhytiumE2000SoCState *s,
                                      unsigned int index,
                                      BlockBackend *blk);

#endif
