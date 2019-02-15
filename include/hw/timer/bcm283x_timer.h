/*
 * Broadcom BCM283x ARM timer variant based on ARM SP804
 * Copyright (c) 2019, Mark <alnyan@airmail.cc>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_TIMER_BCM2835_TIMER_H
#define HW_TIMER_BCM2835_TIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"

/*
 * The datasheet stated 252MHz is the system clock value after reset,
 *  but it may be changed either by device going to sleep mode or
 *  by kernel configuration
 */
#define BCM283x_SYSTEM_CLOCK_FREQ       252000000

#define TYPE_BCM283xTimer "bcm283x_timer"
#define BCM283xTimer(obj) \
    OBJECT_CHECK(BCM283xTimerState, (obj), TYPE_BCM283xTimer)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;

    uint32_t control;
    uint32_t limit;
    uint32_t int_level;
    uint32_t prediv;

    ptimer_state *timer;
    ptimer_state *free_timer;
} BCM283xTimerState;

#endif
