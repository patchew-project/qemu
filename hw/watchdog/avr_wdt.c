/*
 * AVR watchdog
 *
 * Copyright (c) 2018 Michael Rolnik
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/watchdog/avr_wdt.h"
#include "trace.h"

/* Field masks */
#define WDTCSR_MASK_WDP0     0x01
#define WDTCSR_MASK_WDP1     0x02
#define WDTCSR_MASK_WDP2     0x04
#define WDTCSR_MASK_WDE      0x08
#define WDTCSR_MASK_WCE      0x10
#define WDTCSR_MASK_WDP3     0x20
#define WDTCSR_MASK_WDIE     0x40
#define WDTCSR_MASK_WDIF     0x80

#define WDTCSR_SHFT_WDP0     0x00
#define WDTCSR_SHFT_WDP1     0x01
#define WDTCSR_SHFT_WDP2     0x02
#define WDTCSR_SHFT_WDE      0x03
#define WDTCSR_SHFT_WCE      0x04
#define WDTCSR_SHFT_WDP3     0x05
#define WDTCSR_SHFT_WDIE     0x06
#define WDTCSR_SHFT_WDIF     0x07

/* Helper macros */
#define WDP0(csr)       ((csr & WDTCSR_MASK_WDP0) >> WDTCSR_SHFT_WDP0)
#define WDP1(csr)       ((csr & WDTCSR_MASK_WDP1) >> WDTCSR_SHFT_WDP1)
#define WDP2(csr)       ((csr & WDTCSR_MASK_WDP2) >> WDTCSR_SHFT_WDP2)
#define WDP3(csr)       ((csr & WDTCSR_MASK_WDP3) >> WDTCSR_SHFT_WDP3)
#define WDP(csr)        ((WDP3(csr) << 3) | (WDP2(csr) << 2) | \
                         (WDP1(csr) << 1) | (WDP0(csr) << 0))
#define WDIE(csr)       ((csr & WDTCSR_MASK_WDIE) >> WDTCSR_SHFT_WDIE)
#define WDE(csr)        ((csr & WDTCSR_MASK_WDE) >> WDTCSR_SHFT_WDE)
#define WCE(csr)        ((csr & WDTCSR_MASK_WCE) >> WDTCSR_SHFT_WCE)

#define DB_PRINT(fmt, args...) /* Nothing */

#define MS2NS(n)        ((n) * 1000000ull)

static void avr_wdt_reset_alarm(AVRWatchdogState *wdt)
{
    uint32_t csr = wdt->csr;
    int wdp = WDP(csr);
    assert(wdp <= 9);

    if (WDIE(csr) == 0 && WDE(csr) == 0) {
        /* watchdog is stopped */
        return;
    }

    timer_mod_ns(wdt->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
            (MS2NS(15) << wdp));
}

static void avr_wdt_interrupt(void *opaque)
{
    AVRWatchdogState *wdt = opaque;
    int8_t csr = wdt->csr;

    if (WDE(csr) == 0 && WDIE(csr) == 0) {
        /* Stopped */

    } else if (WDE(csr) == 0 && WDIE(csr) == 1) {
        /* Interrupt Mode */
        wdt->csr |= WDTCSR_MASK_WDIF;
        qemu_set_irq(wdt->irq, 1);
        trace_avr_wdt_interrupt();
    } else if (WDE(csr) == 1 && WDIE(csr) == 0) {
        /* System Reset Mode */
    } else if (WDE(csr) == 1 && WDIE(csr) == 1) {
        /* Interrupt and System Reset Mode */
        wdt->csr |= WDTCSR_MASK_WDIF;
        qemu_set_irq(wdt->irq, 1);
        trace_avr_wdt_interrupt();
    }

    avr_wdt_reset_alarm(wdt);
}

static void avr_wdt_reset(DeviceState *dev)
{
    AVRWatchdogState *wdt = AVR_WDT(dev);

    wdt->csr = 0;
    qemu_set_irq(wdt->irq, 0);
    avr_wdt_reset_alarm(wdt);
}

static uint64_t avr_wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    assert(size == 1);
    AVRWatchdogState *wdt = opaque;
    uint8_t retval = wdt->csr;

    trace_avr_wdt_read(offset, retval);

    return (uint64_t)retval;
}

static void avr_wdt_write(void *opaque, hwaddr offset,
                              uint64_t val64, unsigned size)
{
    assert(size == 1);
    AVRWatchdogState *wdt = opaque;
    uint8_t val8 = (uint8_t)val64;

    trace_avr_wdt_write(offset, val8);

    wdt->csr = val8;
    avr_wdt_reset_alarm(wdt);
}

static const MemoryRegionOps avr_wdt_ops = {
    .read = avr_wdt_read,
    .write = avr_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static void avr_wdt_wdr(void *opaque, int irq, int level)
{
    AVRWatchdogState *wdt = AVR_WDT(opaque);

    avr_wdt_reset_alarm(wdt);
}

static void avr_wdt_init(Object *obj)
{
    AVRWatchdogState *s = AVR_WDT(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &avr_wdt_ops,
                          s, "avr-wdt", 0xa);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_in_named(DEVICE(s), avr_wdt_wdr, "wdr", 1);
}

static void avr_wdt_realize(DeviceState *dev, Error **errp)
{
    AVRWatchdogState *s = AVR_WDT(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, avr_wdt_interrupt, s);
}

static void avr_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = avr_wdt_reset;
    dc->realize = avr_wdt_realize;
}

static const TypeInfo avr_wdt_info = {
    .name          = TYPE_AVR_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AVRWatchdogState),
    .instance_init = avr_wdt_init,
    .class_init    = avr_wdt_class_init,
};

static void avr_wdt_register_types(void)
{
    type_register_static(&avr_wdt_info);
}

type_init(avr_wdt_register_types)
