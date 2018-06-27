/*
 * Nordic Semiconductor nRF51  SoC
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef NRF51_SOC_H
#define NRF51_SOC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"

#define TYPE_NRF51_SOC "nrf51-soc"
#define NRF51_SOC(obj) \
    OBJECT_CHECK(NRF51State, (obj), TYPE_NRF51_SOC)

typedef struct NRF51State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    char *kernel_filename;

    ARMv7MState cpu;

    MemoryRegion iomem;
    MemoryRegion sram;
    MemoryRegion flash;

    MemoryRegion *board_memory;

    MemoryRegion container;

} NRF51State;

#endif

