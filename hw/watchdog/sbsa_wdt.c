/*
 * ARM SBSA watchdog emulation
 *
 * Copyright (c) 2020 Linaro Limited
 * Written by Maxim Uvarov
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/watchdog.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/watchdog/sbsa-wdt.h"
#include "migration/vmstate.h"

#define DEFAULT_TIMEOUT  10 /* seconds */
#define TIMERFREQ  62500000 /*arch_timer_get_cntfrq()*/

/* SBSA Generic Watchdog register definitions */
/* refresh frame */
REG32(SBSA_GWDT_WRR, 0x000)

    /* control frame */
REG32(SBSA_GWDT_WCS, 0x000)
    FIELD(SBSA_GWDT_WCS, EN,  0, 1)
    FIELD(SBSA_GWDT_WCS, WS0, 1, 1)
    FIELD(SBSA_GWDT_WCS, WS1, 2, 1)

REG32(SBSA_GWDT_WOR, 0x008)
REG32(SBSA_GWDT_WCV, 0x010)

static inline void log_watchdog(const char *name, uint64_t offset,
                                uint64_t data, unsigned size)
{
    if (0) {
        struct timeval _now;

        gettimeofday(&_now, NULL);
        qemu_log("%d@%zu.%06zu:%s: offset 0x%" PRIx64 " "
                 "data 0x%" PRIx64 " size %u" "\n",
                qemu_get_thread_id(),
                (size_t)_now.tv_sec, (size_t)_now.tv_usec,
                name,
                offset, data, size);
    }
}

static uint64_t sbsa_watchdog_control_read(void *opaque, hwaddr offset,
        unsigned size)
{
    SBSAWatchdog *s = SBSA_WATCHDOG(opaque);
    uint64_t r;

    switch (offset) {
    case A_SBSA_GWDT_WCS:
        r = R_SBSA_GWDT_WCS_WS1_MASK;
        break;
    case A_SBSA_GWDT_WOR:
        r = ptimer_get_count(s->timer) * TIMERFREQ * 2; /* time left */
        break;
    case A_SBSA_GWDT_WCV:
        r = 0; /* TBD */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "SBSA watchdog control read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    log_watchdog(__func__, offset, r, size);
    return r;
}

static void sbsa_watchdog_control_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    SBSAWatchdog *s = SBSA_WATCHDOG(opaque);
    int timeout;

    log_watchdog(__func__, offset, value, size);
    switch (offset) {
    case A_SBSA_GWDT_WCS:
        if (value & R_SBSA_GWDT_WCS_EN_MASK) {
            /* enable watchdog */
            ptimer_transaction_begin(s->timer);
            s->cur_tick = ptimer_get_limit(s->timer);
            ptimer_set_count(s->timer, 0);
            ptimer_run(s->timer, 0);
            ptimer_transaction_commit(s->timer);
            s->enable = 1;
        } else {
            /*
             * never stop to be able to reboot machin,
             * just lower timeout for faster reboot
             */
            ptimer_transaction_begin(s->timer);
            ptimer_set_limit(s->timer, 2, 1);
            s->cur_tick = ptimer_get_limit(s->timer);
            ptimer_set_count(s->timer, 0);
            ptimer_run(s->timer, 0);
            ptimer_transaction_commit(s->timer);
        }

        if (value & R_SBSA_GWDT_WCS_WS0_MASK) {
            /* ignore, write is not used in linux driver */
        }
        if (value & R_SBSA_GWDT_WCS_WS1_MASK) {
            /* ignore, write is not used in linux driver */
        }
        break;
    case A_SBSA_GWDT_WOR:
        timeout = value / TIMERFREQ * 2;
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, value, timeout);
        ptimer_transaction_commit(s->timer);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "SBSA watchdog write: bad offset 0x%x\n",
                (int)offset);
        break;
    }
}

static uint64_t sbsa_watchdog_refresh_read(void *opaque, hwaddr offset,
        unsigned size)
{
    uint64_t r;

    switch (offset) {
    /* No reads from refresh registeres */
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "SBSA watchdog refresh read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    log_watchdog(__func__, offset, r, size);
    return r;
}

static void sbsa_watchdog_refresh_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    SBSAWatchdog *s = SBSA_WATCHDOG(opaque);

    log_watchdog(__func__, offset, value, size);
    switch (offset) {
    case A_SBSA_GWDT_WRR:
        /*
         * Writing WRR for an explicit watchdog refresh.
         * You can write anyting (like 0).
         */
        if (s->enable) {
            ptimer_transaction_begin(s->timer);
            ptimer_set_count(s->timer, 0);
            s->cur_tick = ptimer_get_limit(s->timer);
            ptimer_transaction_commit(s->timer);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "SBSA watchdog refresh write: bad offset 0x%x\n",
                (int)offset);
        break;
    }
}

static const MemoryRegionOps sbsa_watchdog_control_ops = {
    .read = sbsa_watchdog_control_read,
    .write = sbsa_watchdog_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const MemoryRegionOps sbsa_watchdog_refresh_ops = {
    .read = sbsa_watchdog_refresh_read,
    .write = sbsa_watchdog_refresh_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void sbsa_watchdog_tick(void *opaque)
{
    SBSAWatchdog *s = SBSA_WATCHDOG(opaque);

    if (!s->enable) {
        return;
    }

    s->cur_tick--;

    if (!s->cur_tick) {
        watchdog_perform_action();
    }
}

static void sbsa_watchdog_reset(DeviceState *dev)
{
    SBSAWatchdog *s = SBSA_WATCHDOG(dev);

    s->enable = 0;
    /* Set the limit and the count */
    ptimer_transaction_begin(s->timer);
    ptimer_set_limit(s->timer, DEFAULT_TIMEOUT, 1);
    s->cur_tick = ptimer_get_limit(s->timer);
    ptimer_run(s->timer, 0);
    ptimer_transaction_commit(s->timer);
}

static void sbsa_watchdog_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SBSAWatchdog *s = SBSA_WATCHDOG(obj);

    memory_region_init_io(&s->iomem_control, obj, &sbsa_watchdog_control_ops,
            s, "sbsa-wdt-control", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem_control);
    memory_region_init_io(&s->iomem_refresh, obj, &sbsa_watchdog_refresh_ops,
            s, "sbsa-wdt-refresh", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem_refresh);
}

static void sbsa_watchdog_realize(DeviceState *dev, Error **errp)
{
    SBSAWatchdog *s = SBSA_WATCHDOG(dev);

    s->timer = ptimer_init(sbsa_watchdog_tick, s,
                           PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, 1); /* one second */
    ptimer_transaction_commit(s->timer);
}

static const VMStateDescription sbsa_watchdog_vmstate = {
    .name = "sbsa-watchdog",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, SBSAWatchdog),
        VMSTATE_END_OF_LIST()
    }
};

static Property sbsa_watchdog_properties[] = {
    DEFINE_PROP_UINT32("arm,sbsa-gwdt", SBSAWatchdog, timeout_sec, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void sbsa_watchdog_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sbsa_watchdog_realize;
    dc->vmsd = &sbsa_watchdog_vmstate;
    dc->reset = sbsa_watchdog_reset;
    device_class_set_props(dc, sbsa_watchdog_properties);
}

static const TypeInfo sbsa_watchdog_info = {
    .name = TYPE_SBSA_WATCHDOG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SBSAWatchdog),
    .instance_init = sbsa_watchdog_init,
    .class_init = sbsa_watchdog_class_init,
};

static void sbsa_watchdog_register_types(void)
{
    type_register_static(&sbsa_watchdog_info);
}

type_init(sbsa_watchdog_register_types);
