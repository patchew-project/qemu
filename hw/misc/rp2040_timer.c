/*
 * RP2040 timer emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_timer.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define TIMER_TIMEHW       0x00
#define TIMER_TIMELW       0x04
#define TIMER_TIMEHR       0x08
#define TIMER_TIMELR       0x0c
#define TIMER_ALARM0       0x10
#define TIMER_ALARM3       0x1c
#define TIMER_ARMED        0x20
#define TIMER_TIMERAWH     0x24
#define TIMER_TIMERAWL     0x28
#define TIMER_DBGPAUSE     0x2c
#define TIMER_PAUSE        0x30
#define TIMER_INTR         0x34
#define TIMER_INTE         0x38
#define TIMER_INTF         0x3c
#define TIMER_INTS         0x40

#define TIMER_ALARM_MASK   0x0f
#define TIMER_DBGPAUSE_MASK (BIT(2) | BIT(1))
#define TIMER_PAUSE_MASK   BIT(0)

#define ATOMIC_ALIAS_MASK  0x3000
#define ATOMIC_XOR         0x1000
#define ATOMIC_SET         0x2000
#define ATOMIC_CLR         0x3000

static uint32_t rp2040_timer_apply_alias(uint32_t old, uint32_t value,
                                         hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static uint64_t rp2040_timer_now_us(RP2040TimerState *s)
{
    if (s->pause & TIMER_PAUSE_MASK) {
        return s->paused_time_us;
    }

    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000 + s->time_offset_us;
}

static uint32_t rp2040_timer_ints(RP2040TimerState *s)
{
    uint32_t ints = (s->intr & s->inte) | s->intf;

    return ints & TIMER_ALARM_MASK;
}

static void rp2040_timer_update_irq(RP2040TimerState *s)
{
    uint32_t ints = rp2040_timer_ints(s);
    int i;

    for (i = 0; i < RP2040_TIMER_NUM_ALARMS; i++) {
        qemu_set_irq(s->irq[i], !!(ints & BIT(i)));
    }
}

static void rp2040_timer_update_alarm(RP2040TimerState *s, unsigned alarm)
{
    uint64_t now_us;
    uint32_t delta;

    if (!(s->armed & BIT(alarm)) || (s->pause & TIMER_PAUSE_MASK)) {
        timer_del(s->alarm_timer[alarm]);
        return;
    }

    now_us = rp2040_timer_now_us(s);
    delta = s->alarm[alarm] - (uint32_t)now_us;
    if (delta == 0 || delta > INT32_MAX) {
        timer_del(s->alarm_timer[alarm]);
        s->armed &= ~BIT(alarm);
        s->intr |= BIT(alarm);
        rp2040_timer_update_irq(s);
    } else {
        timer_mod(s->alarm_timer[alarm],
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (int64_t)delta * 1000);
    }
}

static void rp2040_timer_update_alarms(RP2040TimerState *s)
{
    int i;

    for (i = 0; i < RP2040_TIMER_NUM_ALARMS; i++) {
        rp2040_timer_update_alarm(s, i);
    }
}

static bool rp2040_timer_alarm_offset(hwaddr offset, unsigned *alarm)
{
    if (offset < TIMER_ALARM0 || offset > TIMER_ALARM3 || (offset & 0x3)) {
        return false;
    }

    *alarm = (offset - TIMER_ALARM0) / sizeof(uint32_t);
    return true;
}

static uint64_t rp2040_timer_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040TimerState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t now_us = rp2040_timer_now_us(s);
    unsigned alarm;
    uint64_t value;

    switch (offset) {
    case TIMER_TIMEHR:
    case TIMER_TIMERAWH:
        value = now_us >> 32;
        break;
    case TIMER_TIMELR:
    case TIMER_TIMERAWL:
        value = (uint32_t)now_us;
        break;
    case TIMER_ARMED:
        value = s->armed;
        break;
    case TIMER_DBGPAUSE:
        value = s->dbgpause;
        break;
    case TIMER_PAUSE:
        value = s->pause;
        break;
    case TIMER_INTR:
        value = s->intr;
        break;
    case TIMER_INTE:
        value = s->inte;
        break;
    case TIMER_INTF:
        value = s->intf;
        break;
    case TIMER_INTS:
        value = rp2040_timer_ints(s);
        break;
    default:
        if (rp2040_timer_alarm_offset(offset, &alarm)) {
            value = s->alarm[alarm];
        } else {
            value = 0;
            rp2040_log_unimplemented_read("timer", size,
                                          RP2040_TIMER_BASE + addr, offset,
                                          value);
        }
        break;
    }

    return value;
}

static void rp2040_timer_set_time(RP2040TimerState *s, uint64_t value)
{
    if (s->pause & TIMER_PAUSE_MASK) {
        s->paused_time_us = value;
    } else {
        s->time_offset_us = value -
                            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000;
    }
    rp2040_timer_update_alarms(s);
}

static void rp2040_timer_write(void *opaque, hwaddr addr,
                               uint64_t value64, unsigned size)
{
    RP2040TimerState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;
    uint32_t old;
    uint64_t now_us;
    unsigned alarm;

    switch (offset) {
    case TIMER_TIMEHW:
        now_us = rp2040_timer_now_us(s);
        rp2040_timer_set_time(s, ((uint64_t)value << 32) |
                              (uint32_t)now_us);
        break;
    case TIMER_TIMELW:
        now_us = rp2040_timer_now_us(s);
        rp2040_timer_set_time(s, (now_us & UINT64_C(0xffffffff00000000)) |
                              value);
        break;
    case TIMER_ARMED:
        s->armed &= ~(value & TIMER_ALARM_MASK);
        rp2040_timer_update_alarms(s);
        break;
    case TIMER_DBGPAUSE:
        s->dbgpause = rp2040_timer_apply_alias(s->dbgpause, value, alias) &
                      TIMER_DBGPAUSE_MASK;
        break;
    case TIMER_PAUSE:
        old = s->pause;
        now_us = rp2040_timer_now_us(s);
        s->pause = rp2040_timer_apply_alias(s->pause, value, alias) &
                   TIMER_PAUSE_MASK;
        if (!(old & TIMER_PAUSE_MASK) && (s->pause & TIMER_PAUSE_MASK)) {
            s->paused_time_us = now_us;
        } else if ((old & TIMER_PAUSE_MASK) &&
                   !(s->pause & TIMER_PAUSE_MASK)) {
            s->time_offset_us = s->paused_time_us -
                                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000;
            rp2040_timer_update_alarms(s);
        }
        break;
    case TIMER_INTR:
        s->intr &= ~(value & TIMER_ALARM_MASK);
        rp2040_timer_update_irq(s);
        break;
    case TIMER_INTE:
        s->inte = rp2040_timer_apply_alias(s->inte, value, alias) &
                  TIMER_ALARM_MASK;
        rp2040_timer_update_irq(s);
        break;
    case TIMER_INTF:
        s->intf = rp2040_timer_apply_alias(s->intf, value, alias) &
                  TIMER_ALARM_MASK;
        rp2040_timer_update_irq(s);
        break;
    default:
        if (rp2040_timer_alarm_offset(offset, &alarm)) {
            s->alarm[alarm] = value;
            s->armed |= BIT(alarm);
            rp2040_timer_update_alarm(s, alarm);
        } else {
            rp2040_log_unimplemented_write("timer", size,
                                           RP2040_TIMER_BASE + addr, offset,
                                           value64);
        }
        break;
    }
}

static const MemoryRegionOps rp2040_timer_ops = {
    .read = rp2040_timer_read,
    .write = rp2040_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_timer_alarm_tick(void *opaque)
{
    uintptr_t packed = (uintptr_t)opaque;
    RP2040TimerState *s = (RP2040TimerState *)(packed & ~(uintptr_t)0x3);
    unsigned alarm = packed & 0x3;

    s->armed &= ~BIT(alarm);
    s->intr |= BIT(alarm);
    rp2040_timer_update_irq(s);
}

static void rp2040_timer_reset(DeviceState *dev)
{
    RP2040TimerState *s = RP2040_TIMER(dev);
    int i;

    s->time_offset_us = -qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000;
    s->paused_time_us = 0;
    memset(s->alarm, 0, sizeof(s->alarm));
    s->armed = 0;
    s->dbgpause = TIMER_DBGPAUSE_MASK;
    s->pause = 0;
    s->intr = 0;
    s->inte = 0;
    s->intf = 0;

    for (i = 0; i < RP2040_TIMER_NUM_ALARMS; i++) {
        timer_del(s->alarm_timer[i]);
        qemu_set_irq(s->irq[i], 0);
    }
}

static void rp2040_timer_init(Object *obj)
{
    RP2040TimerState *s = RP2040_TIMER(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_timer_ops, s,
                          "rp2040.timer", RP2040_TIMER_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[2]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[3]);
}

static void rp2040_timer_realize(DeviceState *dev, Error **errp)
{
    RP2040TimerState *s = RP2040_TIMER(dev);
    int i;

    for (i = 0; i < RP2040_TIMER_NUM_ALARMS; i++) {
        s->alarm_timer[i] =
            timer_new_ns(QEMU_CLOCK_VIRTUAL, rp2040_timer_alarm_tick,
                         (void *)((uintptr_t)s | i));
    }
}

static const VMStateDescription rp2040_timer_vmstate = {
    .name = TYPE_RP2040_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR_ARRAY(alarm_timer, RP2040TimerState,
                                RP2040_TIMER_NUM_ALARMS),
        VMSTATE_INT64(time_offset_us, RP2040TimerState),
        VMSTATE_UINT64(paused_time_us, RP2040TimerState),
        VMSTATE_UINT32_ARRAY(alarm, RP2040TimerState,
                             RP2040_TIMER_NUM_ALARMS),
        VMSTATE_UINT32(armed, RP2040TimerState),
        VMSTATE_UINT32(dbgpause, RP2040TimerState),
        VMSTATE_UINT32(pause, RP2040TimerState),
        VMSTATE_UINT32(intr, RP2040TimerState),
        VMSTATE_UINT32(inte, RP2040TimerState),
        VMSTATE_UINT32(intf, RP2040TimerState),
        VMSTATE_END_OF_LIST()
    },
};

static void rp2040_timer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp2040_timer_realize;
    device_class_set_legacy_reset(dc, rp2040_timer_reset);
    dc->vmsd = &rp2040_timer_vmstate;
}

static const TypeInfo rp2040_timer_info = {
    .name          = TYPE_RP2040_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040TimerState),
    .instance_init = rp2040_timer_init,
    .class_init    = rp2040_timer_class_init,
};

static void rp2040_timer_register_types(void)
{
    type_register_static(&rp2040_timer_info);
}

type_init(rp2040_timer_register_types)
