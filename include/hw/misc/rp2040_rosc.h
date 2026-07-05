/*
 * RP2040 ring oscillator emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_ROSC_H
#define HW_MISC_RP2040_ROSC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_ROSC "rp2040-rosc"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040RoscState, RP2040_ROSC)

#define RP2040_ROSC_BASE 0x40060000
#define RP2040_ROSC_SIZE 0x4000

struct RP2040RoscState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *clk;

    uint32_t ctrl;
    uint32_t freqa;
    uint32_t freqb;
    uint32_t dormant;
    uint32_t div;
    uint32_t phase;
    uint32_t count;
    int64_t count_start_ns;
    uint64_t random_pool;
    uint64_t random_prng_state;
    uint64_t random_seed;
    uint8_t random_pool_bits;
    bool random_seed_set;
    bool badwrite;
};

#endif
