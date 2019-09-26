/*
 * BCM2835 SYS timer emulation
 *
 * Copyright (C) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Datasheet: BCM2835 ARM Peripherals (C6357-M-1398)
 * https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
 *
 * Only the free running 64-bit counter is implemented.
 * The 4 COMPARE registers and the interruption are not implemented.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "hw/timer/bcm2835_systmr.h"
#include "trace.h"

REG32(CTRL_STATUS,  0x00)
REG32(COUNTER_LOW,  0x04)
REG32(COUNTER_HIGH, 0x08)
REG32(COMPARE0,     0x0c)
REG32(COMPARE1,     0x10)
REG32(COMPARE2,     0x14)
REG32(COMPARE3,     0x18)

static uint64_t bcm2835_sys_timer_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    uint64_t r = 0;

    switch (offset) {
    case A_CTRL_STATUS:
    case A_COMPARE0 ... A_COMPARE3:
        break;
    case A_COUNTER_LOW:
    case A_COUNTER_HIGH:
        /* Free running counter at 1MHz */
        r = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
        r >>= 8 * (offset - A_COUNTER_LOW);
        r &= UINT32_MAX;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
    trace_bcm2835_sys_timer_read(offset, r);

    return r;
}

static void bcm2835_sys_timer_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    trace_bcm2835_sys_timer_write(offset, value);

    qemu_log_mask(LOG_UNIMP, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
}

static const MemoryRegionOps bcm2835_sys_timer_ops = {
    .read = bcm2835_sys_timer_read,
    .write = bcm2835_sys_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bcm2835_sys_timer_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835SysTimerState *s = BCM2835_SYSTIMER(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_sys_timer_ops,
                          s, "bcm2835-sys-timer", 0x20);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const TypeInfo bcm2835_sys_timer_info = {
    .name = TYPE_BCM2835_SYSTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835SysTimerState),
    .instance_init = bcm2835_sys_timer_init,
};

static void bcm2835_sys_timer_register_types(void)
{
    type_register_static(&bcm2835_sys_timer_info);
}

type_init(bcm2835_sys_timer_register_types);
