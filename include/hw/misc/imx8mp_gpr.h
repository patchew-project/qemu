/*
 * i.MX 8M Plus IOMUXC GPR
 *
 * Copyright (c) 2026, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>

 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#ifndef IMX8MP_GPR_H
#define IMX8MP_GPR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX8MP_GPR "imx8mp.gpr"
OBJECT_DECLARE_SIMPLE_TYPE(IMX8MPGPRState, IMX8MP_GPR)

enum IMX8MPGPRRegisters {
    IOMUXC_GPR_GPR0  = 0x000 / 4,
    IOMUXC_GPR_GPR1  = 0x004 / 4,
    IOMUXC_GPR_GPR2  = 0x008 / 4,
    IOMUXC_GPR_GPR3  = 0x00C / 4,
    IOMUXC_GPR_GPR4  = 0x010 / 4,
    IOMUXC_GPR_GPR5  = 0x014 / 4,
    IOMUXC_GPR_GPR6  = 0x018 / 4,
    IOMUXC_GPR_GPR7  = 0x01C / 4,
    IOMUXC_GPR_GPR8  = 0x020 / 4,
    IOMUXC_GPR_GPR9  = 0x024 / 4,
    IOMUXC_GPR_GPR10 = 0x028 / 4,
    IOMUXC_GPR_GPR11 = 0x02C / 4,
    IOMUXC_GPR_GPR12 = 0x030 / 4,
    IOMUXC_GPR_GPR13 = 0x034 / 4,
    IOMUXC_GPR_GPR14 = 0x038 / 4,
    IOMUXC_GPR_GPR15 = 0x03C / 4,
    IOMUXC_GPR_GPR16 = 0x040 / 4,
    IOMUXC_GPR_GPR17 = 0x044 / 4,
    IOMUXC_GPR_GPR18 = 0x048 / 4,
    IOMUXC_GPR_GPR19 = 0x04C / 4,
    IOMUXC_GPR_GPR20 = 0x050 / 4,
    IOMUXC_GPR_GPR21 = 0x054 / 4,
    IOMUXC_GPR_GPR22 = 0x058 / 4,
    IOMUXC_GPR_GPR23 = 0x05C / 4,
    IOMUXC_GPR_GPR24 = 0x060 / 4,
    IOMUXC_GPR_MAX,
};

struct IMX8MPGPRState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq cm7_run_irq;
    uint32_t gpr[IOMUXC_GPR_MAX];
};

#endif /* IMX8MP_GPR_H */
