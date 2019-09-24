/*
 * Goldfish virtual platform RTC
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * For more details on Google Goldfish virtual platform refer:
 * https://android.googlesource.com/platform/external/qemu/+/master/docs/GOLDFISH-VIRTUAL-HARDWARE.TXT
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/timer/goldfish_rtc.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

#define RTC_TIME_LOW            0x00
#define RTC_TIME_HIGH           0x04
#define RTC_ALARM_LOW           0x08
#define RTC_ALARM_HIGH          0x0c
#define RTC_IRQ_ENABLED         0x10
#define RTC_CLEAR_ALARM         0x14
#define RTC_ALARM_STATUS        0x18
#define RTC_CLEAR_INTERRUPT     0x1c

static void goldfish_rtc_update(GoldfishRTCState *s)
{
    qemu_set_irq(s->irq, (s->irq_pending & s->irq_enabled) ? 1 : 0);
}

static void goldfish_rtc_interrupt(void *opaque)
{
    GoldfishRTCState *s = (GoldfishRTCState *)opaque;

    s->alarm_running = 0;
    s->irq_pending = 1;
    goldfish_rtc_update(s);
}

static uint64_t goldfish_rtc_get_count(GoldfishRTCState *s)
{
    return s->tick_offset + (uint64_t)qemu_clock_get_ns(rtc_clock);
}

static void goldfish_rtc_clear_alarm(GoldfishRTCState *s)
{
    timer_del(s->timer);
    s->alarm_running = 0;
}

static void goldfish_rtc_set_alarm(GoldfishRTCState *s)
{
    uint64_t ticks = goldfish_rtc_get_count(s);
    uint64_t event = s->alarm_next;

    if (event <= ticks) {
        timer_del(s->timer);
        goldfish_rtc_interrupt(s);
    } else {
        int64_t now = qemu_clock_get_ns(rtc_clock);
        timer_mod(s->timer, now + (event - ticks));
        s->alarm_running = 1;
    }
}

static uint64_t goldfish_rtc_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    GoldfishRTCState *s = (GoldfishRTCState *)opaque;
    uint64_t r;

    switch (offset) {
    case RTC_TIME_LOW:
        r = goldfish_rtc_get_count(s) & 0xffffffff;
        break;
    case RTC_TIME_HIGH:
        r = goldfish_rtc_get_count(s) >> 32;
        break;
    case RTC_ALARM_LOW:
        r = s->alarm_next & 0xffffffff;
        break;
    case RTC_ALARM_HIGH:
        r = s->alarm_next >> 32;
        break;
    case RTC_IRQ_ENABLED:
        r = s->irq_enabled;
        break;
    case RTC_ALARM_STATUS:
        r = s->alarm_running;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "goldfish_rtc_read: Bad offset 0x%x\n", (int)offset);
        r = 0;
        break;
    }

    return r;
}

static void goldfish_rtc_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    uint64_t current_tick, new_tick;
    GoldfishRTCState *s = (GoldfishRTCState *)opaque;

    switch (offset) {
    case RTC_TIME_LOW:
        current_tick = goldfish_rtc_get_count(s);
        new_tick = current_tick & (0xffffffffULL << 32);
        new_tick |= value;
        s->tick_offset += new_tick - current_tick;
        break;
    case RTC_TIME_HIGH:
        current_tick = goldfish_rtc_get_count(s);
        new_tick = current_tick & 0xffffffffULL;
        new_tick |= (value << 32);
        s->tick_offset += new_tick - current_tick;
        break;
    case RTC_ALARM_LOW:
        s->alarm_next &= (0xffffffffULL << 32);
        s->alarm_next |= value;
        goldfish_rtc_set_alarm(s);
        break;
    case RTC_ALARM_HIGH:
        s->alarm_next &= 0xffffffffULL;
        s->alarm_next |= (value << 32);
        break;
    case RTC_IRQ_ENABLED:
        s->irq_enabled = (uint32_t)(value & 0x1);
        goldfish_rtc_update(s);
        break;
    case RTC_CLEAR_ALARM:
        goldfish_rtc_clear_alarm(s);
        break;
    case RTC_CLEAR_INTERRUPT:
        s->irq_pending = 0;
        goldfish_rtc_update(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "goldfish_rtc_write: Bad offset 0x%x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps goldfish_rtc_ops = {
    .read = goldfish_rtc_read,
    .write = goldfish_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void goldfish_rtc_init(Object *obj)
{
    GoldfishRTCState *s = GOLDFISH_RTC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    struct tm tm;

    memory_region_init_io(&s->iomem, obj, &goldfish_rtc_ops, s,
                          "goldfish_rtc", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);

    qemu_get_timedate(&tm, 0);
    s->tick_offset = mktimegm(&tm);
    s->tick_offset *= NANOSECONDS_PER_SECOND;
    s->tick_offset -= qemu_clock_get_ns(rtc_clock);

    s->timer = timer_new_ns(rtc_clock, goldfish_rtc_interrupt, s);
}

static Property goldfish_rtc_properties[] = {
    DEFINE_PROP_UINT64("tick-offset", GoldfishRTCState, tick_offset, 0),
    DEFINE_PROP_UINT64("alarm-next", GoldfishRTCState, alarm_next, 0),
    DEFINE_PROP_UINT32("alarm-running", GoldfishRTCState, alarm_running, 0),
    DEFINE_PROP_UINT32("irq-pending", GoldfishRTCState, irq_pending, 0),
    DEFINE_PROP_UINT32("irq-enabled", GoldfishRTCState, irq_enabled, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void goldfish_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->props = goldfish_rtc_properties;
}

static const TypeInfo goldfish_rtc_info = {
    .name          = TYPE_GOLDFISH_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GoldfishRTCState),
    .instance_init = goldfish_rtc_init,
    .class_init    = goldfish_rtc_class_init,
};

static void goldfish_rtc_register_types(void)
{
    type_register_static(&goldfish_rtc_info);
}

type_init(goldfish_rtc_register_types)
