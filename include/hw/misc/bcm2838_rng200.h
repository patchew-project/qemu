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

typedef union BCM2838Rng200Ctrl {
    uint32_t value;
    struct {
        uint32_t rbg_enable:1;
        uint32_t __r0:12;
        uint32_t div:8;
    };
} BCM2838Rng200Ctrl;

typedef union BCM2838Rng200Int {
    uint32_t value;
    struct {
        uint32_t total_bits_count_irq:1;
        uint32_t __r0:4;
        uint32_t nist_fail_irq:1;
        uint32_t __r1:11;
        uint32_t startup_transition_met_irq:1;
        uint32_t __r2:13;
        uint32_t master_fail_lockout_irq:1;
    };
} BCM2838Rng200Int;

typedef union BCM2838Rng200FifoCount {
    uint32_t value;
    struct {
        uint32_t count:8;
        uint32_t thld:8;
    };
} BCM2838Rng200FifoCount;

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
    BCM2838Rng200Ctrl rng_ctrl;
    BCM2838Rng200Int rng_int_status;
    BCM2838Rng200Int rng_int_enable;
    uint32_t rng_total_bit_count;
    BCM2838Rng200FifoCount rng_fifo_count;
    uint32_t rng_bit_count_threshold;
};

#endif /* BCM2838_RNG200_H */
