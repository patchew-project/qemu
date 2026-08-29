/*
 * RP2040 syscfg emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_SYSCFG_H
#define HW_MISC_RP2040_SYSCFG_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_SYSCFG "rp2040-syscfg"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040SysCfgState, RP2040_SYSCFG)

#define RP2040_SYSCFG_BASE 0x40004000
#define RP2040_SYSCFG_SIZE 0x4000

typedef void (*RP2040SysCfgUpdateFn)(void *opaque);

struct RP2040SysCfgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    RP2040SysCfgUpdateFn update;
    void *update_opaque;
    uint32_t proc0_nmi_mask;
    uint32_t proc1_nmi_mask;
    uint32_t proc_config;
    uint32_t proc_in_sync_bypass;
    uint32_t proc_in_sync_bypass_hi;
    uint32_t dbgforce;
    uint32_t mempowerdown;
};

void rp2040_syscfg_set_update_callback(RP2040SysCfgState *s,
                                       RP2040SysCfgUpdateFn update,
                                       void *opaque);
uint32_t rp2040_syscfg_get_proc0_nmi_mask(RP2040SysCfgState *s);
uint32_t rp2040_syscfg_get_mempowerdown(RP2040SysCfgState *s);

#endif
