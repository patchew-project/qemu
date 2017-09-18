/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX2_WDT_H
#define IMX2_WDT_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"

#define TYPE_IMX2_WDT "imx2.wdt"
#define IMX2_WDT(obj) OBJECT_CHECK(IMX2WdtState, (obj), TYPE_IMX2_WDT)

enum IMX2WdtRegisters {
    IMX2_WDT_WCR,
    IMX2_WDT_REG_NUM
};


typedef struct IMX2WdtState {
    /* <private> */
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint16_t reg[IMX2_WDT_REG_NUM];
} IMX2WdtState;

#endif /* IMX7_SNVS_H */
