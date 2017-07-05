/*
 * Copyright 2016,2017 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _INTC_XIVE_INTERNAL_H
#define _INTC_XIVE_INTERNAL_H

#include <hw/sysbus.h>

struct XIVE {
    SysBusDevice parent;

    /* Properties */
    uint32_t     nr_targets;

    /* IRQ number allocator */
    uint32_t     int_count;     /* Number of interrupts: nr_targets + HW IRQs */
    uint32_t     int_base;      /* Min index */
    uint32_t     int_max;       /* Max index */
    uint32_t     int_hw_bot;    /* Bottom index of HW IRQ allocator */
    uint32_t     int_ipi_top;   /* Highest IPI index handed out so far + 1 */
};

#endif /* _INTC_XIVE_INTERNAL_H */
