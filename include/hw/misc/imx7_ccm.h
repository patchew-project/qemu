/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 CCM, PMU and ANALOG IP blocks emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX7_CCM_H
#define IMX7_CCM_H

#include "hw/misc/imx_ccm.h"
#include "qemu/bitops.h"

#define REG_SET_CLR_TOG(name)  name, name##_SET, name##_CLR, name##_TOG

enum IMX7AnalogRegisters {
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_ARM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_DDR),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_DDR_SS),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_DDR_NUM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_DDR_DENOM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_480),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_480A),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_480B),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_ENET),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_AUDIO),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_AUDIO_SS),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_AUDIO_NUM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_AUDIO_DENOM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_VIDEO),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_VIDEO_SS),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_VIDEO_NUM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_VIDEO_DENOM),
    REG_SET_CLR_TOG(CCM_ANALOG_PLL_MISC0),

    CCM_ANALOG_MAX,

    CCM_ANALOG_PLL_LOCK = BIT(31)
};

enum IMX7CCMRegisters {
    CCM_MAX = 0xBC80 / sizeof(uint32_t),
};

enum IMX7PMURegisters {
    PMU_MAX = 0x140 / sizeof(uint32_t),
};

#undef REG_SET_CLR_TOG

#define TYPE_IMX7_CCM "imx7.ccm"
#define IMX7_CCM(obj) OBJECT_CHECK(IMX7CCMState, (obj), TYPE_IMX7_CCM)

typedef struct IMX6CCMState {
    /* <private> */
    IMXCCMState parent_obj;

    /* <public> */
    struct {
        MemoryRegion container;
        MemoryRegion ccm;
        MemoryRegion pmu;
        MemoryRegion analog;
    } mmio;

    uint32_t ccm[CCM_MAX];
    uint32_t pmu[PMU_MAX];
    uint32_t analog[CCM_ANALOG_MAX];

} IMX7CCMState;

#endif /* IMX7_CCM_H */
