/*
 * QEMU MC146818 RTC emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_RTC_MC146818RTC_H
#define HW_RTC_MC146818RTC_H

#include "hw/isa/isa.h"
#include "qemu/typedefs.h"

#define TYPE_MC146818_RTC "mc146818rtc"

#define RTC_ISA_IRQ 8
#define RTC_ISA_BASE 0x70

ISADevice *mc146818_rtc_init(ISABus *bus, int base_year,
                             qemu_irq intercept_irq);
void rtc_set_memory(ISADevice *dev, int addr, int val);
int rtc_get_memory(ISADevice *dev, int addr);

#endif /* MC146818RTC_H */
