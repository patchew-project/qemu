/*
 * Copyright (c) 2025 Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * i.MX 8MM CCM IP block emulation code
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef IMX8MM_CCM_H
#define IMX8MM_CCM_H

#include "hw/misc/imx_ccm.h"
#include "qom/object.h"

enum IMX8MMCCMRegisters {
    CCM_MAX = 0xc6fc / sizeof(uint32_t) + 1,
};

#define TYPE_IMX8MM_CCM "imx8mm.ccm"
OBJECT_DECLARE_SIMPLE_TYPE(IMX8MMCCMState, IMX8MM_CCM)

struct IMX8MMCCMState {
    IMXCCMState parent_obj;

    MemoryRegion iomem;

    uint32_t ccm[CCM_MAX];
};

#endif /* IMX8MM_CCM_H */
