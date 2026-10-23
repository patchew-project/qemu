/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore System Control Unit (SCU)
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2024 Infineon Technologies AG
 */

#ifndef HW_TRICORE_SCU_H
#define HW_TRICORE_SCU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TRICORE_SCU "tricore_scu"
OBJECT_DECLARE_SIMPLE_TYPE(TriCoreSCUState, TRICORE_SCU)

#define SCU_REG_SIZE                    0x1000
#define SCU_NUM_REGS                    (SCU_REG_SIZE / 4)

struct TriCoreSCUState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* CCU registers */
    uint32_t osccon;
    uint32_t pllstat;
    uint32_t pllcon[3];
    uint32_t plleraystat;
    uint32_t plleraycon[2];
    uint32_t ccucon[6];
    uint32_t fdr;
    uint32_t extcon;

    /* Watchdog stub registers */
    uint32_t wdtscon0;
    uint32_t wdtscon1;
    uint32_t wdtcpu0con0;

    /* Backing store for remaining offsets */
    uint32_t regs[SCU_NUM_REGS];
};

#endif
