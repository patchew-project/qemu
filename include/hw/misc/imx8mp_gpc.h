/*
 * i.MX 8M Plus GPC(General Power Controller)
 *
 * Copyright (c) 2026, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#ifndef IMX8MP_GPC_H
#define IMX8MP_GPC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX8MP_GPC_MMIO_SIZE 0x1000

#define TYPE_IMX8MP_GPC "imx8mp.gpc"

OBJECT_DECLARE_SIMPLE_TYPE(IMX8MPGPCState, IMX8MP_GPC)

enum IMX8MPGPCRegisters {
    IMX8MP_GPC_PU_PGC_SW_PUP_REQ = 0x0D8 / 4,
    IMX8MP_GPC_POLL_REG          = 0x190 / 4,
};

struct IMX8MPGPCState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[IMX8MP_GPC_MMIO_SIZE / 4];
};

#endif /* IMX8MP_GPC_H */
