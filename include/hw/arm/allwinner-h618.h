/*
 * Allwinner H618 System on Chip emulation
 *
 * Copyright (C) 2026 Khai Do <dovankhai97@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_ALLWINNER_H618_H
#define HW_ARM_ALLWINNER_H618_H

#include "qom/object.h"
#include "hw/intc/arm_gic.h"
#include "target/arm/cpu.h"

/**
 * Allwinner H618 device list
 */
enum {
    AW_H618_DEV_SRAM_A1,
    AW_H618_DEV_SRAM_A2,
    AW_H618_DEV_GIC_DIST,
    AW_H618_DEV_GIC_CPU,
    AW_H618_DEV_GIC_HYP,
    AW_H618_DEV_GIC_VCPU,
    AW_H618_DEV_UART0,
    AW_H618_DEV_UART1,
    AW_H618_DEV_UART2,
    AW_H618_DEV_UART3,
    AW_H618_DEV_SDRAM
};

/** Total number of CPU cores in the H618 SoC */
#define AW_H618_NUM_CPUS (4)

/** Object type for the Allwinner H618 SoC */
#define TYPE_AW_H618 "allwinner-h618"

/** Convert input object to Allwinner H618 state object */
OBJECT_DECLARE_SIMPLE_TYPE(AwH618State, AW_H618)

/**
 * Allwinner H618 object
 */
struct AwH618State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpus[AW_H618_NUM_CPUS];
    const hwaddr *memmap;
    GICState gic;
    MemoryRegion sram_a1;
    MemoryRegion sram_a2;
};

#endif /* HW_ARM_ALLWINNER_H618_H */
