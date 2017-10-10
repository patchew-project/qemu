/*
 * BCM2835 System Timer
 *
 * Copyright (C) 2017 Thomas Venriès <thomas.venries@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "trace.h"
#include "hw/timer/bcm2835_systimer.h"

#define ST_SIZE             0x20

#define ST_CONTROL_STATUS   0x00
#define ST_COUNTER_LO       0x04
#define ST_COUNTER_HI       0x08
#define ST_COMPARE0         0x0C
#define ST_COMPARE1         0x10
#define ST_COMPARE2         0x14
#define ST_COMPARE3         0x18

#define TIMER_M0        (1 << 0)
#define TIMER_M1        (1 << 1)
#define TIMER_M2        (1 << 2)
#define TIMER_M3        (1 << 3)
#define TIMER_MATCH(n)  (1 << n)

static void bcm2835_systimer_interrupt(void *opaque, unsigned timer)
{
    BCM2835SysTimerState *s = (BCM2835SysTimerState *)opaque;

    s->ctrl |= TIMER_MATCH(timer);
    qemu_irq_raise((timer == 1) ? s->irq[0] : s->irq[1]);

    trace_bcm2835_systimer_interrupt(timer);
}

static void bcm2835_systimer1_cb(void *opaque)
{
    bcm2835_systimer_interrupt(opaque, 1);
}

static void bcm2835_systimer3_cb(void *opaque)
{
    bcm2835_systimer_interrupt(opaque, 3);
}

static uint64_t bcm2835_systimer_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    BCM2835SysTimerState *s = (BCM2835SysTimerState *)opaque;

    switch (offset) {
    case ST_CONTROL_STATUS:
        return s->ctrl;
    case ST_COUNTER_LO:
        return (uint64_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) & 0xffffffff;
    case ST_COUNTER_HI:
        return (uint64_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) >> 32;
    case ST_COMPARE0:
        return s->cmp0;
    case ST_COMPARE1:
        return s->cmp1;
    case ST_COMPARE2:
        return s->cmp2;
    case ST_COMPARE3:
        return s->cmp3;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_systimer_read: Bad offset - [%x]\n",
                      (int)offset);
        return 0;
    }
}

static void bcm2835_systimer_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    BCM2835SysTimerState *s = (BCM2835SysTimerState *)opaque;

    switch (offset) {
    case ST_CONTROL_STATUS:
        if ((s->ctrl & TIMER_M1) && (value & TIMER_M1)) {
            qemu_irq_lower(s->irq[0]);
            s->ctrl &= ~TIMER_M1;
        }
        if ((s->ctrl & TIMER_M3) && (value & TIMER_M3)) {
            qemu_irq_lower(s->irq[1]);
            s->ctrl &= ~TIMER_M3;
        }
        break;
    case ST_COMPARE0:
        s->cmp0 = value;
        break;
    case ST_COMPARE1:
        timer_mod(s->timers[0], value);
        s->cmp1 = value;
        break;
    case ST_COMPARE2:
        s->cmp2 = value;
        break;
    case ST_COMPARE3:
        timer_mod(s->timers[1], value);
        s->cmp3 = value;
        break;

    case ST_COUNTER_LO:
    case ST_COUNTER_HI:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_systimer_write: Read-only offset %x\n",
                      (int)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_systimer_write: Bad offset %x\n",
                      (int)offset);
    }
}

static const MemoryRegionOps bcm2835_systimer_ops = {
    .read = bcm2835_systimer_read,
    .write = bcm2835_systimer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_systimer = {
    .name = TYPE_BCM2835_SYSTIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp0, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp1, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp2, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp3, BCM2835SysTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_systimer_init(Object *obj)
{
    BCM2835SysTimerState *s = BCM2835_SYSTIMER(obj);

    s->ctrl = 0;
    s->cmp0 = s->cmp1 = s->cmp2 = s->cmp3 = 0;

    s->timers[0] = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_systimer1_cb, s);
    s->timers[1] = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_systimer3_cb, s);

    memory_region_init_io(&s->iomem, obj, &bcm2835_systimer_ops, s,
                          TYPE_BCM2835_SYSTIMER, ST_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
}

static void bcm2835_systimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "BCM2835 System Timer";
    dc->vmsd = &vmstate_bcm2835_systimer;
}

static TypeInfo bcm2835_systimer_info = {
    .name          = TYPE_BCM2835_SYSTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835SysTimerState),
    .class_init    = bcm2835_systimer_class_init,
    .instance_init = bcm2835_systimer_init,
};

static void bcm2835_systimer_register_types(void)
{
    type_register_static(&bcm2835_systimer_info);
}

type_init(bcm2835_systimer_register_types)
