/*
 * AVR watchdog
 *
 * Copyright (c) 2021 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef HW_WATCHDOG_AVR_WDT_H
#define HW_WATCHDOG_AVR_WDT_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "qom/object.h"

#define TYPE_AVR_WDT "avr-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(AVRWatchdogState, AVR_WDT)

struct AVRWatchdogState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    MemoryRegion imsk_iomem;
    MemoryRegion ifr_iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    /* registers */
    uint8_t csr;
};

#endif /* HW_WATCHDOG_AVR_WDT_H */
