/*
 * Renesas RX Realtime Clock (RTC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 26 (RTC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * Calendar-count-mode model. The time/date registers are BCD-encoded and the
 * counter advances once per second (driven by a host timer) while RCR2.START is
 * set. Seeded from host wall-clock time at reset. Periodic/alarm/carry
 * interrupts are not generated.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/bcd.h"
#include "hw/rtc/renesas_rx_rtc.h"
#include "migration/vmstate.h"
#include "system/rtc.h"
#include "system/system.h"

/* Register offsets from the RTC base (0x0008C400), calendar count mode. */
#define R_R64CNT    0x00    /* 8-bit  64 Hz counter (read-only)  */
#define R_RSECCNT   0x02    /* 8-bit  seconds  (BCD)             */
#define R_RMINCNT   0x04    /* 8-bit  minutes  (BCD)             */
#define R_RHRCNT    0x06    /* 8-bit  hours    (BCD)             */
#define R_RWKCNT    0x08    /* 8-bit  day of week                */
#define R_RDAYCNT   0x0A    /* 8-bit  day      (BCD)             */
#define R_RMONCNT   0x0C    /* 8-bit  month    (BCD)             */
#define R_RYRCNT    0x0E    /* 16-bit year     (BCD)             */
#define R_RSECAR    0x10
#define R_RMINAR    0x12
#define R_RHRAR     0x14
#define R_RWKAR     0x16
#define R_RDAYAR    0x18
#define R_RMONAR    0x1A
#define R_RYRAR     0x1C
#define R_RCR1      0x22    /* 8-bit  control 1                  */
#define R_RCR2      0x24    /* 8-bit  control 2                  */
#define R_RCR3      0x26    /* 8-bit  control 3                  */
#define R_RCR4      0x28    /* 8-bit  control 4                  */

#define RCR2_START  (1u << 0)
#define RCR2_RESET  (1u << 1)

static int days_in_month(int mon, int year)
{
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return 29;
    }
    return d[(mon - 1) % 12];
}

static void rtc_seed_from_host(RenesasRxRtcState *s)
{
    struct tm now;

    qemu_get_timedate(&now, 0);
    s->sec = now.tm_sec;
    s->min = now.tm_min;
    s->hour = now.tm_hour;
    s->wday = now.tm_wday;
    s->mday = now.tm_mday;
    s->mon = now.tm_mon + 1;
    s->year = now.tm_year + 1900;
}

static void rtc_advance_second(RenesasRxRtcState *s)
{
    if (++s->sec < 60) {
        return;
    }
    s->sec = 0;
    if (++s->min < 60) {
        return;
    }
    s->min = 0;
    if (++s->hour < 24) {
        return;
    }
    s->hour = 0;
    s->wday = (s->wday + 1) % 7;
    if (++s->mday <= days_in_month(s->mon, s->year)) {
        return;
    }
    s->mday = 1;
    if (++s->mon <= 12) {
        return;
    }
    s->mon = 1;
    s->year++;
}

static void rtc_timer_tick(void *opaque)
{
    RenesasRxRtcState *s = opaque;

    if (!(s->rcr2 & RCR2_START)) {
        return;
    }
    rtc_advance_second(s);
    timer_mod(s->timer,
              qemu_clock_get_ms(rtc_clock) + 1000);
}

static void rtc_arm_timer(RenesasRxRtcState *s)
{
    if (s->rcr2 & RCR2_START) {
        timer_mod(s->timer, qemu_clock_get_ms(rtc_clock) + 1000);
    } else {
        timer_del(s->timer);
    }
}

static uint64_t rtc_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxRtcState *s = opaque;

    switch (offset) {
    case R_R64CNT:
        return 0;
    case R_RSECCNT:
        return to_bcd(s->sec);
    case R_RMINCNT:
        return to_bcd(s->min);
    case R_RHRCNT:
        return to_bcd(s->hour);
    case R_RWKCNT:
        return s->wday;
    case R_RDAYCNT:
        return to_bcd(s->mday);
    case R_RMONCNT:
        return to_bcd(s->mon);
    case R_RYRCNT:
        return to_bcd(s->year % 100) | (to_bcd(s->year / 100) << 8);
    case R_RSECAR:
        return s->secar;
    case R_RMINAR:
        return s->minar;
    case R_RHRAR:
        return s->hrar;
    case R_RWKAR:
        return s->wkar;
    case R_RDAYAR:
        return s->dayar;
    case R_RMONAR:
        return s->monar;
    case R_RYRAR:
        return s->yrar;
    case R_RCR1:
        return s->rcr1;
    case R_RCR2:
        return s->rcr2;
    case R_RCR3:
        return s->rcr3;
    case R_RCR4:
        return s->rcr4;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-rtc: read unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void rtc_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    RenesasRxRtcState *s = opaque;

    switch (offset) {
    case R_RSECCNT:
        s->sec = from_bcd(value & 0x7f);
        break;
    case R_RMINCNT:
        s->min = from_bcd(value & 0x7f);
        break;
    case R_RHRCNT:
        s->hour = from_bcd(value & 0x3f);
        break;
    case R_RWKCNT:
        s->wday = value & 7;
        break;
    case R_RDAYCNT:
        s->mday = from_bcd(value & 0x3f);
        break;
    case R_RMONCNT:
        s->mon = from_bcd(value & 0x1f);
        break;
    case R_RYRCNT:
        s->year = 2000 + from_bcd(value & 0xff);
        break;
    case R_RSECAR:
        s->secar = value;
        break;
    case R_RMINAR:
        s->minar = value;
        break;
    case R_RHRAR:
        s->hrar = value;
        break;
    case R_RWKAR:
        s->wkar = value;
        break;
    case R_RDAYAR:
        s->dayar = value;
        break;
    case R_RMONAR:
        s->monar = value;
        break;
    case R_RYRAR:
        s->yrar = value;
        break;
    case R_RCR1:
        s->rcr1 = value;
        break;
    case R_RCR2:
        if (value & RCR2_RESET) {
            s->sec = s->min = s->hour = 0;
        }
        s->rcr2 = value & ~RCR2_RESET;
        rtc_arm_timer(s);
        break;
    case R_RCR3:
        s->rcr3 = value;
        break;
    case R_RCR4:
        s->rcr4 = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-rtc: write unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps rtc_ops = {
    .read = rtc_read,
    .write = rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 2 },
    .valid = { .min_access_size = 1, .max_access_size = 2 },
};

static void rx_rtc_reset(DeviceState *dev)
{
    RenesasRxRtcState *s = RENESAS_RX_RTC(dev);

    rtc_seed_from_host(s);
    s->secar = s->minar = s->hrar = s->wkar = s->dayar = s->monar = 0;
    s->yrar = 0;
    s->rcr1 = 0;
    s->rcr2 = 0;
    s->rcr3 = 0;
    s->rcr4 = 0;
    timer_del(s->timer);
}

static void rx_rtc_realize(DeviceState *dev, Error **errp)
{
    RenesasRxRtcState *s = RENESAS_RX_RTC(dev);

    s->timer = timer_new_ms(rtc_clock, rtc_timer_tick, s);
}

static void rx_rtc_init(Object *obj)
{
    RenesasRxRtcState *s = RENESAS_RX_RTC(obj);

    memory_region_init_io(&s->mr, obj, &rtc_ops, s,
                          "renesas-rx-rtc", RX_RTC_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static const VMStateDescription vmstate_rx_rtc = {
    .name = "renesas-rx-rtc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(sec, RenesasRxRtcState),
        VMSTATE_INT32(min, RenesasRxRtcState),
        VMSTATE_INT32(hour, RenesasRxRtcState),
        VMSTATE_INT32(wday, RenesasRxRtcState),
        VMSTATE_INT32(mday, RenesasRxRtcState),
        VMSTATE_INT32(mon, RenesasRxRtcState),
        VMSTATE_INT32(year, RenesasRxRtcState),
        VMSTATE_UINT8(secar, RenesasRxRtcState),
        VMSTATE_UINT8(minar, RenesasRxRtcState),
        VMSTATE_UINT8(hrar, RenesasRxRtcState),
        VMSTATE_UINT8(wkar, RenesasRxRtcState),
        VMSTATE_UINT8(dayar, RenesasRxRtcState),
        VMSTATE_UINT8(monar, RenesasRxRtcState),
        VMSTATE_UINT16(yrar, RenesasRxRtcState),
        VMSTATE_UINT8(rcr1, RenesasRxRtcState),
        VMSTATE_UINT8(rcr2, RenesasRxRtcState),
        VMSTATE_UINT8(rcr3, RenesasRxRtcState),
        VMSTATE_UINT8(rcr4, RenesasRxRtcState),
        VMSTATE_TIMER_PTR(timer, RenesasRxRtcState),
        VMSTATE_END_OF_LIST()
    }
};

static void rx_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rx_rtc_realize;
    dc->vmsd = &vmstate_rx_rtc;
    device_class_set_legacy_reset(dc, rx_rtc_reset);
}

static const TypeInfo rx_rtc_info = {
    .name = TYPE_RENESAS_RX_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxRtcState),
    .instance_init = rx_rtc_init,
    .class_init = rx_rtc_class_init,
};

static void rx_rtc_register_types(void)
{
    type_register_static(&rx_rtc_info);
}

type_init(rx_rtc_register_types)
