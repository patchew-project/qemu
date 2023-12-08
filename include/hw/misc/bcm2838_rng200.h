/*
 * BCM2838 Random Number Generator emulation
 *
 * Copyright (C) 2022 Sergey Pushkarev <sergey.pushkarev@auriga.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2838_RNG200_H
#define BCM2838_RNG200_H

#include <stdbool.h>
#include "qom/object.h"
#include "qemu/fifo8.h"
#include "sysemu/rng.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/qdev-clock.h"
#include "hw/irq.h"

#define TYPE_BCM2838_RNG200 "bcm2838-rng200"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838Rng200State, BCM2838_RNG200)

typedef struct {
    uint32_t ctrl;
    uint32_t int_status;
    uint32_t fifo_count;
    uint32_t fifo_count_threshold;
    uint32_t total_bit_count_threshold;
} BCM2838_rng_regs_t;

struct BCM2838Rng200State {
    SysBusDevice busdev;
    MemoryRegion iomem;

    ptimer_state *ptimer;
    RngBackend *rng;
    Clock *clock;

    uint32_t rbg_period;
    uint32_t rng_fifo_cap;
    bool use_timer;

    Fifo8    fifo;
    qemu_irq irq;

    BCM2838_rng_regs_t regs;
};

#endif /* BCM2838_RNG200_H */
