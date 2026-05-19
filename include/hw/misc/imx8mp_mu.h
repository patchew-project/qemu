/*
 * NXP i.MX 8M Plus  Messaging Unit (MU)
 *
 * Copyright (c) 2026, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#ifndef IMX8MP_MU_H
#define IMX8MP_MU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX8MP_MU_MMIO_SIZE 0x10000

enum IMX8MPMURegisters {
    MU_TR0     = 0x000 / 4,
    MU_TR1     = 0x004 / 4,
    MU_TR2     = 0x008 / 4,
    MU_TR3     = 0x00C / 4,
    MU_RR0     = 0x010 / 4,
    MU_RR1     = 0x014 / 4,
    MU_RR2     = 0x018 / 4,
    MU_RR3     = 0x01C / 4,
    MU_SR      = 0x020 / 4,
    MU_CR      = 0x024 / 4,
    MU_MAX,
};

#define TYPE_IMX8MP_MU "imx8mp-mu"
OBJECT_DECLARE_SIMPLE_TYPE(IMX8MPMUState, IMX8MP_MU)

typedef struct IMX8MPMUState {
    SysBusDevice parent_obj;

    struct {
        MemoryRegion container;
        MemoryRegion regs;
    } mmio;

    qemu_irq irq;

    struct IMX8MPMUState *peer;
    uint32_t mu[MU_MAX];

    bool strict_access;

} IMX8MPMUState;

#endif /* IMX8MP_MU_H */
