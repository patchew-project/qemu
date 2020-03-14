/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "sysemu/watchdog.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/watchdog/wdt_imx2.h"

static void imx2_wdt_interrupt(void *opaque)
{
    IMX2WdtState *s = IMX2_WDT(opaque);

    s->wicr |= IMX2_WDT_WICR_WTIS;
    qemu_set_irq(s->irq, 1);
}

static void imx2_wdt_expired(void *opaque)
{
    IMX2WdtState *s = IMX2_WDT(opaque);

    s->wrsr = IMX2_WDT_WRSR_TOUT;

    /* Perform watchdog action if watchdog is enabled */
    if (s->wcr & IMX2_WDT_WCR_WDE) {
        watchdog_perform_action();
    }
}

static void imx2_wdt_reset(DeviceState *dev)
{
    IMX2WdtState *s = IMX2_WDT(dev);

    s->wcr = IMX2_WDT_WCR_WDA | IMX2_WDT_WCR_SRS;
    s->wsr = 0;
    s->wrsr &= ~(IMX2_WDT_WRSR_TOUT | IMX2_WDT_WRSR_SFTW);
    s->wicr = 4;
    s->wmcr = IMX2_WDT_WMCR_PDE;
}

static uint64_t imx2_wdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    IMX2WdtState *s = IMX2_WDT(opaque);

    switch (addr) {
    case IMX2_WDT_WCR:
        return s->wcr;
    case IMX2_WDT_WSR:
        return s->wsr;
    case IMX2_WDT_WRSR:
        return s->wrsr;
    case IMX2_WDT_WICR:
        return s->wicr;
    case IMX2_WDT_WMCR:
        return s->wmcr;
    }
    return 0;
}

static void imx_wdt2_update_itimer(IMX2WdtState *s, bool start)
{
    bool running = (s->wcr & IMX2_WDT_WCR_WDE) && (s->wcr & IMX2_WDT_WCR_WT);
    bool enabled = s->wicr & IMX2_WDT_WICR_WIE;

    ptimer_transaction_begin(s->itimer);
    if (start || !enabled) {
        ptimer_stop(s->itimer);
    }
    if (running && enabled) {
        int count = ptimer_get_count(s->timer);
        int pretimeout = s->wicr & IMX2_WDT_WICR_WICT;

        /*
         * Only (re-)start pretimeout timer if its counter value is larger
         * than 0. Otherwise it will fire right away and we'll get an
         * interrupt loop.
         */
        if (count > pretimeout) {
            ptimer_set_count(s->itimer, count - pretimeout);
            if (start) {
                ptimer_run(s->itimer, 1);
            }
        }
    }
    ptimer_transaction_commit(s->itimer);
}

static void imx_wdt2_update_timer(IMX2WdtState *s, bool start)
{
    ptimer_transaction_begin(s->timer);
    if (start) {
        ptimer_stop(s->timer);
    }
    if ((s->wcr & IMX2_WDT_WCR_WDE) && (s->wcr & IMX2_WDT_WCR_WT)) {
        int count = (s->wcr & IMX2_WDT_WCR_WT) >> 8;

        ptimer_set_count(s->timer, count);
        if (start) {
            ptimer_run(s->timer, 1);
        }
    }
    ptimer_transaction_commit(s->timer);
    if (s->pretimeout_support) {
        imx_wdt2_update_itimer(s, start);
    }
}

static void imx2_wdt_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned int size)
{
    IMX2WdtState *s = IMX2_WDT(opaque);

    switch (addr) {
    case IMX2_WDT_WCR:
        s->wcr = value;
        if (!(value & IMX2_WDT_WCR_SRS)) {
            s->wrsr = IMX2_WDT_WRSR_SFTW;
        }
        if (!(value & (IMX2_WDT_WCR_WDA | IMX2_WDT_WCR_SRS)) ||
            (!(value & IMX2_WDT_WCR_WT) && (value & IMX2_WDT_WCR_WDE))) {
            watchdog_perform_action();
        }
        s->wcr |= IMX2_WDT_WCR_SRS;
        imx_wdt2_update_timer(s, true);
        break;
    case IMX2_WDT_WSR:
        if (s->wsr == IMX2_WDT_SEQ1 && value == IMX2_WDT_SEQ2) {
            imx_wdt2_update_timer(s, false);
        }
        s->wsr = value;
        break;
    case IMX2_WDT_WRSR:
        break;
    case IMX2_WDT_WICR:
        if (!s->pretimeout_support) {
            return;
        }
        /* The pretimeout value is write-once */
        if (s->pretimeout_locked) {
            value &= ~IMX2_WDT_WICR_WICT;
            s->wicr &= (IMX2_WDT_WICR_WTIS | IMX2_WDT_WICR_WICT);
        } else {
            s->wicr &= IMX2_WDT_WICR_WTIS;
        }
        s->wicr |= value & (IMX2_WDT_WICR_WIE | IMX2_WDT_WICR_WICT);
        if (value & IMX2_WDT_WICR_WTIS) {
            s->wicr &= ~IMX2_WDT_WICR_WTIS;
            qemu_set_irq(s->irq, 0);
        }
        imx_wdt2_update_itimer(s, true);
        s->pretimeout_locked = true;
        break;
    case IMX2_WDT_WMCR:
        s->wmcr = value & IMX2_WDT_WMCR_PDE;
        break;
    }
}

static const MemoryRegionOps imx2_wdt_ops = {
    .read  = imx2_wdt_read,
    .write = imx2_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the
         * real device but in practice there is no reason for a guest
         * to access this device unaligned.
         */
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static const VMStateDescription vmstate_imx2_wdt = {
    .name = "imx2.wdt",
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, IMX2WdtState),
        VMSTATE_PTIMER(itimer, IMX2WdtState),
        VMSTATE_BOOL(pretimeout_locked, IMX2WdtState),
        VMSTATE_UINT16(wcr, IMX2WdtState),
        VMSTATE_UINT16(wsr, IMX2WdtState),
        VMSTATE_UINT16(wrsr, IMX2WdtState),
        VMSTATE_UINT16(wmcr, IMX2WdtState),
        VMSTATE_UINT16(wicr, IMX2WdtState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx2_wdt_realize(DeviceState *dev, Error **errp)
{
    IMX2WdtState *s = IMX2_WDT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &imx2_wdt_ops, s,
                          TYPE_IMX2_WDT,
                          IMX2_WDT_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    s->timer = ptimer_init(imx2_wdt_expired, s, PTIMER_POLICY_DEFAULT);
    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, 2);
    ptimer_set_limit(s->timer, 0xff, 1);
    ptimer_transaction_commit(s->timer);
    if (s->pretimeout_support) {
        s->itimer = ptimer_init(imx2_wdt_interrupt, s, PTIMER_POLICY_DEFAULT);
        ptimer_transaction_begin(s->itimer);
        ptimer_set_freq(s->itimer, 2);
        ptimer_set_limit(s->itimer, 0xff, 1);
        ptimer_transaction_commit(s->itimer);
    }
}

static Property imx2_wdt_properties[] = {
    DEFINE_PROP_BOOL("pretimeout-support", IMX2WdtState, pretimeout_support,
                     false),
};

static void imx2_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, imx2_wdt_properties);
    dc->realize = imx2_wdt_realize;
    dc->reset = imx2_wdt_reset;
    dc->vmsd = &vmstate_imx2_wdt;
    dc->desc = "i.MX watchdog timer";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo imx2_wdt_info = {
    .name          = TYPE_IMX2_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX2WdtState),
    .class_init    = imx2_wdt_class_init,
};

static WatchdogTimerModel model = {
    .wdt_name = "imx2-watchdog",
    .wdt_description = "i.MX2 Watchdog",
};

static void imx2_wdt_register_type(void)
{
    watchdog_add_model(&model);
    type_register_static(&imx2_wdt_info);
}
type_init(imx2_wdt_register_type)
