/*
 * Microchip PolarFire SoC RTC
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/rtc/mchp_pfsoc_rtc.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/rtc.h"
#include "system/system.h"

#define RTC_MMIO_SIZE               0x1000
#define RTC_CONTROL_MASK            0x000007ff
#define RTC_MODE_MASK               0x0000000f
#define RTC_PRESCALER_MASK          0x03ffffff
#define RTC_ALARM_UPPER_MASK        0x000007ff
#define RTC_UPPER_MASK              0x3fffffff
#define RTC_CALENDAR_MASK           0x000000ff

#define RTC_CONTROL                 0x00
#define RTC_CONTROL_START_RUNNING   BIT(0)
#define RTC_CONTROL_STOP            BIT(1)
#define RTC_CONTROL_ALARM_ON        BIT(2)
#define RTC_CONTROL_ALARM_OFF       BIT(3)
#define RTC_CONTROL_RESET           BIT(4)
#define RTC_CONTROL_UPLOAD          BIT(5)
#define RTC_CONTROL_DOWNLOAD        BIT(6)
#define RTC_CONTROL_MATCH           BIT(7)
#define RTC_CONTROL_WAKEUP_CLEAR    BIT(8)
#define RTC_CONTROL_WAKEUP_SET      BIT(9)
#define RTC_CONTROL_UPDATED         BIT(10)

#define RTC_MODE                    0x04
#define RTC_MODE_CLOCK_CALENDAR     BIT(0)
#define RTC_MODE_WAKEUP_ENABLE      BIT(1)
#define RTC_MODE_WAKEUP_RESET       BIT(2)
#define RTC_MODE_WAKEUP_CONTINUE    BIT(3)

#define RTC_PRESCALER               0x08
#define RTC_ALARM_LOWER             0x0c
#define RTC_ALARM_UPPER             0x10
#define RTC_COMPARE_LOWER           0x14
#define RTC_COMPARE_UPPER           0x18
#define RTC_DATETIME_LOWER          0x20
#define RTC_DATETIME_UPPER          0x24
#define RTC_SECONDS                 0x30
#define RTC_MINUTES                 0x34
#define RTC_HOURS                   0x38
#define RTC_DAY                     0x3c
#define RTC_MONTH                   0x40
#define RTC_YEAR                    0x44
#define RTC_WEEKDAY                 0x48
#define RTC_WEEK                    0x4c
#define RTC_SECONDS_COUNT           0x50
#define RTC_MINUTES_COUNT           0x54
#define RTC_HOURS_COUNT             0x58
#define RTC_DAY_COUNT               0x5c
#define RTC_MONTH_COUNT             0x60
#define RTC_YEAR_COUNT              0x64
#define RTC_WEEKDAY_COUNT           0x68
#define RTC_WEEK_COUNT              0x6c

#define RTC_REG(offset) ((offset) / sizeof(uint32_t))

static uint64_t mchp_pfsoc_rtc_get_count(MchpPfSoCRtcState *s)
{
    if (!s->running) {
        return s->frozen_count;
    }

    return s->tick_offset +
           qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;
}

static void mchp_pfsoc_rtc_set_count(MchpPfSoCRtcState *s, uint64_t count)
{
    if (s->running) {
        s->tick_offset = (int64_t)count -
            qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;
    } else {
        s->frozen_count = count;
    }
}

static void mchp_pfsoc_rtc_update_irq(MchpPfSoCRtcState *s)
{
    qemu_set_irq(s->irq_wakeup, s->wakeup_pending);
    qemu_set_irq(s->irq_match, s->match_pending);
}

static void mchp_pfsoc_rtc_schedule_alarm(MchpPfSoCRtcState *s);

static void mchp_pfsoc_rtc_alarm(void *opaque)
{
    MchpPfSoCRtcState *s = opaque;
    bool periodic = s->regs[RTC_REG(RTC_MODE)] & RTC_MODE_WAKEUP_RESET;

    s->alarm_enabled = periodic;
    s->match_pending = true;
    if (s->regs[RTC_REG(RTC_MODE)] & RTC_MODE_WAKEUP_ENABLE) {
        s->wakeup_pending = true;
    }
    if (s->regs[RTC_REG(RTC_MODE)] & RTC_MODE_WAKEUP_RESET) {
        mchp_pfsoc_rtc_set_count(s, 0);
    }
    if (!(s->regs[RTC_REG(RTC_MODE)] & RTC_MODE_WAKEUP_CONTINUE)) {
        s->frozen_count = mchp_pfsoc_rtc_get_count(s);
        s->running = false;
    }
    mchp_pfsoc_rtc_update_irq(s);
    if (periodic) {
        mchp_pfsoc_rtc_schedule_alarm(s);
    }
}

static void mchp_pfsoc_rtc_schedule_alarm(MchpPfSoCRtcState *s)
{
    uint64_t compare_mask;
    uint64_t target;
    uint64_t now;
    uint64_t delta;

    timer_del(s->alarm_timer);
    if (!s->alarm_enabled || !s->running ||
        (s->regs[RTC_REG(RTC_MODE)] & RTC_MODE_CLOCK_CALENDAR)) {
        return;
    }

    compare_mask = s->regs[RTC_REG(RTC_COMPARE_LOWER)] |
        ((uint64_t)(s->regs[RTC_REG(RTC_COMPARE_UPPER)] &
                    RTC_UPPER_MASK) << 32);
    if (compare_mask != MAKE_64BIT_MASK(0, 62)) {
        return;
    }

    target = s->regs[RTC_REG(RTC_ALARM_LOWER)] |
        ((uint64_t)(s->regs[RTC_REG(RTC_ALARM_UPPER)] &
                    RTC_ALARM_UPPER_MASK) << 32);
    now = mchp_pfsoc_rtc_get_count(s);
    if (target <= now) {
        mchp_pfsoc_rtc_alarm(s);
        return;
    }

    delta = target - now;
    if (delta > INT64_MAX / NANOSECONDS_PER_SECOND) {
        return;
    }
    timer_mod(s->alarm_timer,
              qemu_clock_get_ns(rtc_clock) + delta * NANOSECONDS_PER_SECOND);
}

static void mchp_pfsoc_rtc_control_write(MchpPfSoCRtcState *s,
                                         uint32_t value)
{
    uint64_t count;

    value &= RTC_CONTROL_MASK;
    if (value & RTC_CONTROL_UPDATED) {
        s->updated = false;
    }
    if (value & RTC_CONTROL_RESET) {
        mchp_pfsoc_rtc_set_count(s, 0);
        s->updated = true;
    }
    if (value & RTC_CONTROL_UPLOAD) {
        count = s->regs[RTC_REG(RTC_DATETIME_LOWER)] |
            ((uint64_t)(s->regs[RTC_REG(RTC_DATETIME_UPPER)] &
                        RTC_UPPER_MASK) << 32);
        mchp_pfsoc_rtc_set_count(s, count);
        s->updated = true;
    }
    if (value & RTC_CONTROL_DOWNLOAD) {
        count = mchp_pfsoc_rtc_get_count(s);
        s->regs[RTC_REG(RTC_DATETIME_LOWER)] = count;
        s->regs[RTC_REG(RTC_DATETIME_UPPER)] = count >> 32;
    }
    if ((value & RTC_CONTROL_START_RUNNING) && !s->running) {
        s->tick_offset = (int64_t)s->frozen_count -
            qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;
        s->running = true;
    }
    if ((value & RTC_CONTROL_STOP) && s->running) {
        s->frozen_count = mchp_pfsoc_rtc_get_count(s);
        s->running = false;
    }
    if (value & RTC_CONTROL_ALARM_OFF) {
        s->alarm_enabled = false;
        s->wakeup_pending = false;
        s->match_pending = false;
        timer_del(s->alarm_timer);
    } else if (value & RTC_CONTROL_ALARM_ON) {
        s->alarm_enabled = true;
    }
    if (value & RTC_CONTROL_WAKEUP_CLEAR) {
        s->wakeup_pending = false;
        s->match_pending = false;
    }
    if (value & RTC_CONTROL_WAKEUP_SET) {
        s->wakeup_pending = true;
    }

    mchp_pfsoc_rtc_update_irq(s);
    mchp_pfsoc_rtc_schedule_alarm(s);
}

static uint32_t mchp_pfsoc_rtc_control_read(MchpPfSoCRtcState *s)
{
    uint32_t value;

    value = s->running ? RTC_CONTROL_START_RUNNING : 0;
    if (s->alarm_enabled) {
        value |= RTC_CONTROL_ALARM_ON;
    }
    if (s->match_pending) {
        value |= RTC_CONTROL_MATCH;
    }
    if (s->updated) {
        value |= RTC_CONTROL_UPDATED;
    }
    return value;
}

static uint32_t mchp_pfsoc_rtc_datetime_lower_read(MchpPfSoCRtcState *s)
{
    s->read_latch = mchp_pfsoc_rtc_get_count(s);
    s->read_latch_valid = true;
    return (uint32_t)s->read_latch;
}

static uint32_t mchp_pfsoc_rtc_datetime_upper_read(MchpPfSoCRtcState *s)
{
    uint64_t count = s->read_latch_valid ? s->read_latch :
                     mchp_pfsoc_rtc_get_count(s);

    s->read_latch_valid = false;
    return (count >> 32) & RTC_UPPER_MASK;
}

static uint64_t mchp_pfsoc_rtc_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    MchpPfSoCRtcState *s = opaque;

    switch (offset) {
    case RTC_CONTROL:
        return mchp_pfsoc_rtc_control_read(s);
    case RTC_DATETIME_LOWER:
        return mchp_pfsoc_rtc_datetime_lower_read(s);
    case RTC_DATETIME_UPPER:
        return mchp_pfsoc_rtc_datetime_upper_read(s);
    case RTC_MODE:
    case RTC_PRESCALER:
    case RTC_ALARM_LOWER:
    case RTC_ALARM_UPPER:
    case RTC_COMPARE_LOWER:
    case RTC_COMPARE_UPPER:
    case RTC_SECONDS:
    case RTC_MINUTES:
    case RTC_HOURS:
    case RTC_DAY:
    case RTC_MONTH:
    case RTC_YEAR:
    case RTC_WEEKDAY:
    case RTC_WEEK:
    case RTC_SECONDS_COUNT:
    case RTC_MINUTES_COUNT:
    case RTC_HOURS_COUNT:
    case RTC_DAY_COUNT:
    case RTC_MONTH_COUNT:
    case RTC_YEAR_COUNT:
    case RTC_WEEKDAY_COUNT:
    case RTC_WEEK_COUNT:
        return s->regs[RTC_REG(offset)];
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                      "(size %d, offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, offset);
        return 0;
    }
}

static void mchp_pfsoc_rtc_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    MchpPfSoCRtcState *s = opaque;
    bool update_alarm = false;

    switch (offset) {
    case RTC_CONTROL:
        mchp_pfsoc_rtc_control_write(s, value);
        return;
    case RTC_MODE:
        s->regs[RTC_REG(offset)] = value & RTC_MODE_MASK;
        update_alarm = true;
        break;
    case RTC_PRESCALER:
        s->regs[RTC_REG(offset)] = value & RTC_PRESCALER_MASK;
        break;
    case RTC_ALARM_LOWER:
    case RTC_COMPARE_LOWER:
        s->regs[RTC_REG(offset)] = value;
        update_alarm = true;
        break;
    case RTC_ALARM_UPPER:
        s->regs[RTC_REG(offset)] = value & RTC_ALARM_UPPER_MASK;
        update_alarm = true;
        break;
    case RTC_COMPARE_UPPER:
        s->regs[RTC_REG(offset)] = value & RTC_UPPER_MASK;
        update_alarm = true;
        break;
    case RTC_DATETIME_LOWER:
        s->regs[RTC_REG(offset)] = value;
        break;
    case RTC_DATETIME_UPPER:
        s->regs[RTC_REG(offset)] = value & RTC_UPPER_MASK;
        break;
    case RTC_SECONDS:
    case RTC_MINUTES:
    case RTC_HOURS:
    case RTC_DAY:
    case RTC_MONTH:
    case RTC_YEAR:
    case RTC_WEEKDAY:
    case RTC_WEEK:
        s->regs[RTC_REG(offset)] = value & RTC_CALENDAR_MASK;
        break;
    case RTC_SECONDS_COUNT:
    case RTC_MINUTES_COUNT:
    case RTC_HOURS_COUNT:
    case RTC_DAY_COUNT:
    case RTC_MONTH_COUNT:
    case RTC_YEAR_COUNT:
    case RTC_WEEKDAY_COUNT:
    case RTC_WEEK_COUNT:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                      "(size %d, value 0x%" PRIx64
                      ", offset 0x%" HWADDR_PRIx ")\n",
                      __func__, size, value, offset);
        return;
    }

    if (update_alarm) {
        mchp_pfsoc_rtc_schedule_alarm(s);
    }
}

static const MemoryRegionOps mchp_pfsoc_rtc_ops = {
    .read = mchp_pfsoc_rtc_read,
    .write = mchp_pfsoc_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void mchp_pfsoc_rtc_reset_hold(Object *obj, ResetType type)
{
    MchpPfSoCRtcState *s = MCHP_PFSOC_RTC(obj);

    timer_del(s->alarm_timer);
    s->running = false;
    s->alarm_enabled = false;
    s->wakeup_pending = false;
    s->match_pending = false;
    s->updated = false;
    s->read_latch_valid = false;
    memset(s->regs, 0, sizeof(s->regs));

    s->tick_offset = 0;
    s->frozen_count = 0;
    s->read_latch = 0;
    mchp_pfsoc_rtc_update_irq(s);
}

static int mchp_pfsoc_rtc_pre_save(void *opaque)
{
    MchpPfSoCRtcState *s = opaque;
    int64_t delta = qemu_clock_get_ns(rtc_clock) /
                    NANOSECONDS_PER_SECOND -
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) /
                    NANOSECONDS_PER_SECOND;

    s->tick_offset_vmstate = s->tick_offset + delta;
    return 0;
}

static int mchp_pfsoc_rtc_post_load(void *opaque, int version_id)
{
    MchpPfSoCRtcState *s = opaque;
    int64_t delta = qemu_clock_get_ns(rtc_clock) /
                    NANOSECONDS_PER_SECOND -
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) /
                    NANOSECONDS_PER_SECOND;

    s->tick_offset = s->tick_offset_vmstate - delta;
    s->read_latch_valid = false;
    mchp_pfsoc_rtc_update_irq(s);
    mchp_pfsoc_rtc_schedule_alarm(s);
    return 0;
}

static const VMStateDescription vmstate_mchp_pfsoc_rtc = {
    .name = TYPE_MCHP_PFSOC_RTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = mchp_pfsoc_rtc_pre_save,
    .post_load = mchp_pfsoc_rtc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MchpPfSoCRtcState,
                             MCHP_PFSOC_RTC_REGS),
        VMSTATE_INT64(tick_offset_vmstate, MchpPfSoCRtcState),
        VMSTATE_UINT64(frozen_count, MchpPfSoCRtcState),
        VMSTATE_BOOL(running, MchpPfSoCRtcState),
        VMSTATE_BOOL(alarm_enabled, MchpPfSoCRtcState),
        VMSTATE_BOOL(wakeup_pending, MchpPfSoCRtcState),
        VMSTATE_BOOL(match_pending, MchpPfSoCRtcState),
        VMSTATE_BOOL(updated, MchpPfSoCRtcState),
        VMSTATE_END_OF_LIST()
    },
};

static void mchp_pfsoc_rtc_init(Object *obj)
{
    MchpPfSoCRtcState *s = MCHP_PFSOC_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &mchp_pfsoc_rtc_ops, s,
                          TYPE_MCHP_PFSOC_RTC, RTC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_wakeup);
    sysbus_init_irq(sbd, &s->irq_match);
    s->alarm_timer = timer_new_ns(rtc_clock, mchp_pfsoc_rtc_alarm, s);
}

static void mchp_pfsoc_rtc_finalize(Object *obj)
{
    MchpPfSoCRtcState *s = MCHP_PFSOC_RTC(obj);

    timer_free(s->alarm_timer);
}

static void mchp_pfsoc_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = mchp_pfsoc_rtc_reset_hold;
    dc->vmsd = &vmstate_mchp_pfsoc_rtc;
}

static const TypeInfo mchp_pfsoc_rtc_info = {
    .name = TYPE_MCHP_PFSOC_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCRtcState),
    .instance_init = mchp_pfsoc_rtc_init,
    .instance_finalize = mchp_pfsoc_rtc_finalize,
    .class_init = mchp_pfsoc_rtc_class_init,
};

static void mchp_pfsoc_rtc_register_types(void)
{
    type_register_static(&mchp_pfsoc_rtc_info);
}

type_init(mchp_pfsoc_rtc_register_types)
