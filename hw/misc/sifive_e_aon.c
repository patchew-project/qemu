/*
 * SiFive HiFive1 AON (Always On Domain) for QEMU.
 *
 * Copyright (c) 2022 SiFive, Inc. All rights reserved.
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
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "sysemu/watchdog.h"
#include "qapi/visitor.h"
#include "hw/misc/sifive_e_aon.h"

static void sifive_e_aon_wdt_update_wdogcount(SiFiveEAONState *r)
{
    if (0 == r->wdogcfg.wdogenalways &&
        0 == r->wdogcfg.wdogencoreawake) {
        return;
    }
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    r->wdogcount += muldiv64(now - r->wdog_restart_time,
                             r->wdogclk_freq, NANOSECONDS_PER_SECOND);
    /* Clean the most significant bit. */
    r->wdogcount = ((r->wdogcount << 1) >> 1);
    r->wdog_restart_time = now;
}

static void sifive_e_aon_wdt_update_state(SiFiveEAONState *r)
{
    sifive_e_aon_wdt_update_wdogcount(r);
    uint16_t wdogs = (uint16_t)(r->wdogcount >> r->wdogcfg.wdogscale);
    bool cmp_signal = false;
    if (wdogs >= r->wdogcmp0) {
        cmp_signal = true;
        if (1 == r->wdogcfg.wdogzerocmp) {
            r->wdogcount = 0;
            wdogs = 0;
        }
    }

    if (cmp_signal) {
        if (1 == r->wdogcfg.wdogrsten) {
            watchdog_perform_action();
        }
        r->wdogcfg.wdogip0 = 1;
    }

    qemu_set_irq(r->wdog_irq, r->wdogcfg.wdogip0);

    if (wdogs < r->wdogcmp0 &&
        (r->wdogcfg.wdogenalways ||
         r->wdogcfg.wdogencoreawake)) {
        int64_t next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next += muldiv64((r->wdogcmp0 - wdogs) << r->wdogcfg.wdogscale,
                         NANOSECONDS_PER_SECOND, r->wdogclk_freq);
        timer_mod(r->wdog_timer, next);
    } else {
        timer_mod(r->wdog_timer, INT64_MAX);
    }
}

/*
 * Callback used when the timer set using timer_mod expires.
 */
static void sifive_e_aon_wdt_expired_cb(void *opaque)
{
    SiFiveEAONState *r = SIFIVE_E_AON(opaque);
    sifive_e_aon_wdt_update_state(r);
}

static uint64_t
sifive_e_aon_wdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    SiFiveEAONState *r = SIFIVE_E_AON(opaque);

    switch (addr) {
    case SIFIVE_E_AON_WDT_WDOGCFG:
        return r->wdogcfg.value;
    case SIFIVE_E_AON_WDT_WDOGCOUNT:
        sifive_e_aon_wdt_update_wdogcount(r);
        return r->wdogcount;
    case SIFIVE_E_AON_WDT_WDOGS:
        sifive_e_aon_wdt_update_wdogcount(r);
        return r->wdogcount >> r->wdogcfg.wdogscale;
    case SIFIVE_E_AON_WDT_WDOGFEED:
        return 0;
    case SIFIVE_E_AON_WDT_WDOGKEY:
        return r->wdogunlock;
    case SIFIVE_E_AON_WDT_WDOGCMP0:
        return r->wdogcmp0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
                      __func__, (int)addr);
    }

    return 0;
}

static void
sifive_e_aon_wdt_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    SiFiveEAONState *r = SIFIVE_E_AON(opaque);
    uint32_t value = val64;

    switch (addr) {
    case SIFIVE_E_AON_WDT_WDOGCFG:
        if (0 == r->wdogunlock) {
            return;
        }
        wdogcfg_s new_wdogcfg;
        new_wdogcfg.value = value;
        if (1 == (r->wdogcfg.wdogenalways ||
                  r->wdogcfg.wdogencoreawake) &&
            0 == (new_wdogcfg.wdogenalways ||
                  new_wdogcfg.wdogencoreawake)) {
            sifive_e_aon_wdt_update_wdogcount(r);
        } else if (0 == (r->wdogcfg.wdogenalways ||
                         r->wdogcfg.wdogencoreawake) &&
                   1 == (new_wdogcfg.wdogenalways ||
                         new_wdogcfg.wdogencoreawake)) {
            r->wdog_restart_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        r->wdogcfg.value = value;
        r->wdogunlock = 0;
        break;
    case SIFIVE_E_AON_WDT_WDOGCOUNT:
        if (0 == r->wdogunlock) {
            return;
        }
        r->wdogcount = ((value << 1) >> 1);
        r->wdog_restart_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        r->wdogunlock = 0;
        break;
    case SIFIVE_E_AON_WDT_WDOGS:
        return;
    case SIFIVE_E_AON_WDT_WDOGFEED:
        if (0 == r->wdogunlock) {
            return;
        }
        if (SIFIVE_E_AON_WDOGFEED == value) {
            r->wdogcount = 0;
            r->wdog_restart_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        r->wdogunlock = 0;
        break;
    case SIFIVE_E_AON_WDT_WDOGKEY:
        if (SIFIVE_E_AON_WDOGKEY == value) {
            r->wdogunlock = 1;
        }
        break;
    case SIFIVE_E_AON_WDT_WDOGCMP0:
        if (0 == r->wdogunlock) {
            return;
        }
        r->wdogcmp0 = (uint16_t) value;
        r->wdogunlock = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n",
                      __func__, (int)addr, (int)value);
    }

    sifive_e_aon_wdt_update_state(r);
}

static uint64_t
sifive_e_aon_read(void *opaque, hwaddr addr, unsigned int size)
{
    if (addr < SIFIVE_E_AON_RTC_RTCCFG) {
        /* AON WDT */
        return sifive_e_aon_wdt_read(opaque, addr, size);
    } else if (addr < SIFIVE_E_AON_LFROSC_LFROSCCFG) {
        /* AON RTC */
    } else if (addr < SIFIVE_E_AON_BACKUP_BACKUP0) {
        /* AON LFROSC */
    } else if (addr < SIFIVE_E_AON_PMU_PMUWAKEUP0) {
        /* AON BACKUP */
    } else if (addr < SIFIVE_E_AON_MAX) {
        /* AON PMU */
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
            __func__, (int)addr);
    }

    return 0;
}

static void
sifive_e_aon_write(void *opaque, hwaddr addr,
                   uint64_t val64, unsigned int size)
{
    if (addr < SIFIVE_E_AON_RTC_RTCCFG) {
        /* AON WDT */
        sifive_e_aon_wdt_write(opaque, addr, val64, size);
    } else if (addr < SIFIVE_E_AON_LFROSC_LFROSCCFG) {
        /* AON RTC */
    } else if (addr < SIFIVE_E_AON_BACKUP_BACKUP0) {
        /* AON LFROSC */
    } else if (addr < SIFIVE_E_AON_PMU_PMUWAKEUP0) {
        /* AON BACKUP */
    } else if (addr < SIFIVE_E_AON_MAX) {
        /* AON PMU */
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
                      __func__, (int)addr);
    }
}

static const MemoryRegionOps sifive_e_aon_ops = {
    .read = sifive_e_aon_read,
    .write = sifive_e_aon_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

SiFiveEAONState *sifive_e_aon_create(MemoryRegion *mr, hwaddr base,
                                     qemu_irq wdt_irq)
{
    DeviceState *dev;
    SysBusDevice *s;
    SiFiveEAONState *r;

    dev = qdev_new(TYPE_SIFIVE_E_AON);
    s = SYS_BUS_DEVICE(dev);

    sysbus_realize_and_unref(s, &error_fatal);

    memory_region_add_subregion(mr, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, wdt_irq);

    r = SIFIVE_E_AON(dev);
    return r;
}

static void sifive_e_aon_reset(DeviceState *dev)
{
    SiFiveEAONState *s = SIFIVE_E_AON(dev);

    s->wdogcfg.wdogrsten = 0;
    s->wdogcfg.wdogenalways = 0;
    s->wdogcfg.wdogencoreawake = 0;
    s->wdogcmp0 = 0xbeef;
}

static void sifive_e_aon_wdt_update_wdogclk_freq(Object *obj, Visitor *v,
                                                 const char *name,
                                                 void *opaque,
                                                 Error **errp) {
    SiFiveEAONState *r = SIFIVE_E_AON(opaque);
    uint64_t value;
    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    r->wdogclk_freq = value;
    sifive_e_aon_wdt_update_state(r);
}

static void sifive_e_aon_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SiFiveEAONState *s = SIFIVE_E_AON(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &sifive_e_aon_ops, s,
                          TYPE_SIFIVE_E_AON, SIFIVE_E_AON_MAX);
    sysbus_init_mmio(sbd, &s->mmio);

    /* watchdog timer */
    s->wdogclk_freq = SIFIVE_E_LFCLK_DEFAULT_FREQ;
    sysbus_init_irq(sbd, &s->wdog_irq);
    object_property_add(obj, "wdogclk-frequency", "uint64",
                        NULL,
                        sifive_e_aon_wdt_update_wdogclk_freq,
                        NULL, s);
}

static void sifive_e_aon_realize(DeviceState *dev, Error **errp)
{
    SiFiveEAONState *s = SIFIVE_E_AON(dev);

    /* watchdog timer */
    s->wdog_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 sifive_e_aon_wdt_expired_cb, s);
}

static void sifive_e_aon_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sifive_e_aon_realize;
    dc->reset = sifive_e_aon_reset;
}

static const TypeInfo sifive_e_aon_info = {
    .name          = TYPE_SIFIVE_E_AON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveEAONState),
    .instance_init = sifive_e_aon_init,
    .class_init    = sifive_e_aon_class_init,
};

static void sifive_e_aon_register_types(void)
{
    type_register_static(&sifive_e_aon_info);
}

type_init(sifive_e_aon_register_types)
