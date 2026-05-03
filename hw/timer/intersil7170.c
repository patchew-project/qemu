/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Intersil 7170 Real Time Clock and Timer Emulation
 *
 * This device mimics the core functionality of the Intersil 7170 RTC,
 * specifically targeting the 1/100th second periodic interrupt requested
 * by the Sun-3 Boot PROM diagnostic routines.
 */

#include "qemu/osdep.h"

#include "hw/timer/intersil7170.h"
#include "hw/core/irq.h"
#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(Intersil7170State, INTERSIL_7170)

struct Intersil7170State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    QEMUTimer *timer;

    /* Registers */
    uint8_t int_status; /* 0x10 */
    uint8_t int_mask;
    uint8_t command; /* 0x11 */
};

#define REG_INT 0x10
#define REG_CMD 0x11

/* Interrupt Register bits */
#define RTC_INT_PENDING 0x80
#define RTC_INT_DAY 0x40
#define RTC_INT_HOUR 0x20
#define RTC_INT_MIN 0x10
#define RTC_INT_SEC 0x08
#define RTC_INT_TSEC 0x04
#define RTC_INT_HSEC 0x02
#define RTC_INT_ALARM 0x01

/* Command Register bits */
#define RTC_CMD_INTENA 0x10
#define RTC_CMD_RUN 0x08

static void intersil7170_update_irq(Intersil7170State *s)
{
    bool level = (s->int_status & s->int_mask) && (s->command & RTC_CMD_INTENA);

    if (level) {
        s->int_status |= RTC_INT_PENDING;
    } else {
        s->int_status &= ~RTC_INT_PENDING;
    }

    qemu_set_irq(s->irq, level);
}

static void intersil7170_timer_cb(void *opaque)
{
    Intersil7170State *s = opaque;

    if (!(s->command & RTC_CMD_RUN)) {
        return;
    }

    /*
     * Timer fired. Set pending bit based on what is unmasked.
     * The Sun-3 PROM primarily demands the Hundredth-Second
     * (RTC_INT_HSEC) tick.
     */
    if (s->int_mask & RTC_INT_HSEC) {
        s->int_status |= RTC_INT_HSEC;
        intersil7170_update_irq(s);

        /* Reschedule for 1/100th of a second (10,000,000 ns) */
        timer_mod(s->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000000);
    }
}

static uint64_t intersil7170_read(void *opaque, hwaddr addr, unsigned size)
{
    Intersil7170State *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case REG_INT:
        val = s->int_status;
        /*
         * Reading the interrupt register formally clears all
         * pending interrupts.
         */
        s->int_status = 0;
        intersil7170_update_irq(s);
        break;
    case REG_CMD:
        val = s->command;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void intersil7170_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    Intersil7170State *s = opaque;

    switch (addr) {
    case REG_INT:
        /*
         * Writing to the INT register sets the mask!
         * Pending flag is read-only.
         */
        s->int_mask = val & ~RTC_INT_PENDING;
        intersil7170_update_irq(s);

        /* If timer requires starting, schedule immediately */
        if ((s->command & RTC_CMD_RUN) && (s->int_mask & RTC_INT_HSEC)) {
            timer_mod(s->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000000);
        }
        break;
    case REG_CMD:
        s->command = val;
        intersil7170_update_irq(s);

        if ((s->command & RTC_CMD_RUN) && (s->int_mask & RTC_INT_HSEC)) {
            timer_mod(s->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000000);
        } else if (!(s->command & RTC_CMD_RUN)) {
            timer_del(s->timer);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps intersil7170_ops = {
    .read = intersil7170_read,
    .write = intersil7170_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
            .unaligned = true,
        },
    .valid = {
            .min_access_size = 1,
            .max_access_size = 4,
            .unaligned = true,
        },
};

static void intersil7170_realize(DeviceState *dev, Error **errp)
{
    Intersil7170State *s = INTERSIL_7170(OBJECT(dev));

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, intersil7170_timer_cb, s);
}

static void intersil7170_reset(DeviceState *dev)
{
    Intersil7170State *s = INTERSIL_7170(OBJECT(dev));

    s->int_status = 0;
    s->int_mask = 0;
    s->command = 0;
    timer_del(s->timer);
}

static void intersil7170_init(Object *obj)
{
    Intersil7170State *s = INTERSIL_7170(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &intersil7170_ops, s, "intersil7170",
                        8192);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void intersil7170_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = intersil7170_realize;
    device_class_set_legacy_reset(dc, intersil7170_reset);
}

static const TypeInfo intersil7170_info = {
    .name = TYPE_INTERSIL_7170,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Intersil7170State),
    .instance_init = intersil7170_init,
    .class_init = intersil7170_class_init,
};

static void intersil7170_register_types(void)
{
    type_register_static(&intersil7170_info);
}

type_init(intersil7170_register_types)
