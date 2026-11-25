/*
 * Spacemit K3 Syscons
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#ifndef HW_MISC_SPACEMIT_K3_SYSCON_H
#define HW_MISC_SPACEMIT_K3_SYSCON_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "system/memory.h"
#include <stdint.h>

#define SPACEMIT_K3_CPU_CONFIG_SIZE 0x400
#define SPACEMIT_K3_SYSCON_MPMU_SIZE 0x10000
#define SPACEMIT_K3_SYSCON_APMU_SIZE 0x400
#define SPACEMIT_K3_DDRC_SIZE 0x1000000

#define TYPE_SPACEMIT_K3_SYSCON_MPMU_UNIT "spacemit-k3-syscon-mpmu"
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3SysconMPMUState,
                           SPACEMIT_K3_SYSCON_MPMU_UNIT)
#define TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT "spacemit-k3-syscon-apmu"
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3SysconAPMUState,
                           SPACEMIT_K3_SYSCON_APMU_UNIT)
#define TYPE_SPACEMIT_K3_DDRC "spacemit-k3-ddrc"
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3DDRCState, SPACEMIT_K3_DDRC)
#define TYPE_SPACEMIT_K3_CPU_CONFIG "spacemit-k3-cpu-config"
OBJECT_DECLARE_SIMPLE_TYPE(SpacemitK3CPUConfigState, SPACEMIT_K3_CPU_CONFIG)


struct SpacemitK3SysconMPMUState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio;
};

struct SpacemitK3SysconAPMUState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio;
};

struct SpacemitK3DDRCState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio;

    /* Register at offset 0xb40010 that stores some state */
    uint32_t b40010;
};

struct SpacemitK3CPUConfigState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio;
    uint32_t regs[SPACEMIT_K3_CPU_CONFIG_SIZE >> 2];
    int boot_mode;
};

enum {
    SPACEMIT_K3_BOOT_MODE_EMMC,
    SPACEMIT_K3_BOOT_MODE_NOR,
    SPACEMIT_K3_BOOT_MODE_NAND,
    SPACEMIT_K3_BOOT_MODE_UFS,
    SPACEMIT_K3_BOOT_MODE_USB,
    SPACEMIT_K3_BOOT_MODE_UART,
};

enum {
    SPACEMIT_K3_SELECTED_BOOT_MODE_EMMC,
    SPACEMIT_K3_SELECTED_BOOT_MODE_NOR,
    SPACEMIT_K3_SELECTED_BOOT_MODE_UFS,
    SPACEMIT_K3_SELECTED_BOOT_MODE_SD,
};

void spacemit_k3_set_selected_boot_mode(SpacemitK3CPUConfigState *s,
                                        int selected_boot_mode);

#endif /* HW_MISC_SPACEMIT_K3_SYSCON_H */
