/*
 * Xilinx Zynq cadence TTC model
 *
 * Copyright (c) 2011 Xilinx Inc.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written By Haibing Ma
 *            M. Habib
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CADENCE_TTC_H
#define CADENCE_TTC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

struct CadenceTimerState {
    QEMUTimer *timer;
    int freq;

    uint32_t reg_clock;
    uint32_t reg_count;
    uint32_t reg_value;
    uint16_t reg_interval;
    uint16_t reg_match[3];
    uint32_t reg_intr;
    uint32_t reg_intr_en;
    uint32_t reg_event_ctrl;
    uint32_t reg_event;

    uint64_t cpu_time;
    unsigned int cpu_time_valid;

    qemu_irq irq;
};

typedef struct CadenceTimerState CadenceTimerState;

struct CadenceTTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    CadenceTimerState timer[3];
};

typedef struct CadenceTTCState CadenceTTCState;

#define TYPE_CADENCE_TTC "cadence_ttc"
#define CADENCE_TTC(obj) \
    OBJECT_CHECK(CadenceTTCState, (obj), TYPE_CADENCE_TTC)

#endif /* CADENCE_TTC_H */
