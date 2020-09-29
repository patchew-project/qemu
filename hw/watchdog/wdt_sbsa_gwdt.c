/*
 * Generic watchdog device model for SBSA
 *
 * Copyright Linaro.org 2020
 *
 * Authors:
 *  Shashi Mallela <shashi.mallela@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/reset.h"
#include "sysemu/watchdog.h"
#include "hw/watchdog/wdt_sbsa_gwdt.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

static WatchdogTimerModel model = {
    .wdt_name = TYPE_WDT_SBSA_GWDT,
    .wdt_description = "sbsa_gwdt device for sbsa_ref platform",
};

static const VMStateDescription vmstate_sbsa_gwdt = {
    .name = "vmstate_sbsa_gwdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, SBSA_GWDTState),
        VMSTATE_BOOL(enabled, SBSA_GWDTState),
        VMSTATE_BOOL(ws0, SBSA_GWDTState),
        VMSTATE_BOOL(ws1, SBSA_GWDTState),
        VMSTATE_UINT32(wrr, SBSA_GWDTState),
        VMSTATE_UINT32(wcs, SBSA_GWDTState),
        VMSTATE_UINT32(worl, SBSA_GWDTState),
        VMSTATE_UINT32(woru, SBSA_GWDTState),
        VMSTATE_UINT32(wcvl, SBSA_GWDTState),
        VMSTATE_UINT32(wcvu, SBSA_GWDTState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t sbsa_gwdt_rread(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t ret;

    if (addr == SBSA_GWDT_WRR) {
        /* watch refresh read has no effect and returns 0 */
        ret = 0;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in refresh frame read :"
                        " 0x%x\n", (int)addr);
    }
    return ret;
}

static uint64_t sbsa_gwdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    SBSA_GWDTState *s = SBSA_GWDT(opaque);
    uint32_t ret = 0;

    switch (addr) {
    case SBSA_GWDT_WCS:
        ret = s->wcs;
        break;
    case SBSA_GWDT_WOR:
        ret = s->worl;
        break;
    case SBSA_GWDT_WORU:
         ret = s->woru;
         break;
    case SBSA_GWDT_WCV:
        ret = s->wcvl;
        break;
    case SBSA_GWDT_WCVU:
        ret = s->wcvu;
        break;
    case SBSA_GWDT_W_IIDR:
        ret = s->id;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in control frame read :"
                        " 0x%x\n", (int)addr);
    }
    return ret;
}

static void sbsa_gwdt_update_timer(SBSA_GWDTState *s, bool refresh,
                                   bool expired)
{
    uint64_t timeout = 0;

    if (s->enabled) {
        if ((refresh) || (expired && !s->ws0)) {
            /*
             * Extract the upper 16 bits from woru & 32 bits from worl
             * registers to construct the 48 bit offset value
             */
            timeout = s->woru & SBSA_GWDT_WOR_MASK;
            timeout <<= 32;
            timeout |= s->worl;
            timeout += qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            /* store the current timeout value into compare registers */
            s->wcvu = timeout >> 32;
            s->wcvl = timeout;
        } else {
            timeout = s->wcvu;
            timeout <<= 32;
            timeout |= s->wcvl;
            timeout += qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }

        if (!s->ws0) {
            timer_del(s->timer);
            timer_mod(s->ptimer,
                       timeout * NANOSECONDS_PER_SECOND);
        } else {
            timer_del(s->ptimer);
            timer_mod(s->timer,
                       timeout * NANOSECONDS_PER_SECOND);
        }
    } else {
        timer_del(s->ptimer);
        timer_del(s->timer);
    }
}

static void sbsa_gwdt_rwrite(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size) {
    SBSA_GWDTState *s = SBSA_GWDT(opaque);

    if (offset == SBSA_GWDT_WRR) {
        s->wrr = data;
        s->wcs &= ~SBSA_GWDT_WCS_WS0;
        s->wcs &= ~SBSA_GWDT_WCS_WS1;
        s->ws0 = false;
        s->ws1 = false;
        sbsa_gwdt_update_timer(s, true , false);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in refresh frame write :"
                        " 0x%x\n", (int)offset);
    }
}

static void sbsa_gwdt_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size) {
    SBSA_GWDTState *s = SBSA_GWDT(opaque);
    bool enable;

    switch (offset) {
    case SBSA_GWDT_WCS:
        enable = data & SBSA_GWDT_WCS_EN;
        if (enable) {
            s->wcs |= SBSA_GWDT_WCS_EN;
            s->enabled = true;
        } else {
            s->wcs &= ~SBSA_GWDT_WCS_EN;
            s->enabled = false;
            s->ws0 = false;
            s->ws1 = false;
        }
        sbsa_gwdt_update_timer(s, false, false);
        break;

    case SBSA_GWDT_WOR:
        s->worl = data;
        /*
         * TODO:- setting woru to 0 and triggering update timer(below) is a
         * temporary workaround to handle current linux driver which is
         * based on earlier version of BSA specification.Once the linux
         * driver is updated to BSA v0.9 will remove these next 2 lines.
         */
        s->woru = 0;
        sbsa_gwdt_update_timer(s, true , false);
        break;

    case SBSA_GWDT_WORU:
        s->woru = data;
        sbsa_gwdt_update_timer(s, true , false);
        break;

    case SBSA_GWDT_WCV:
        s->wcvl = data;
        break;

    case SBSA_GWDT_WCVU:
        s->wcvu = data;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in control frame write :"
                " 0x%x\n", (int)offset);
    }
    return;
}

static void wdt_sbsa_gwdt_reset(DeviceState *dev)
{
    SBSA_GWDTState *s = SBSA_GWDT(dev);

    timer_del(s->ptimer);
    timer_del(s->timer);

    s->enabled = false;
    s->ws0 = false;
    s->ws1 = false;
    s->wcs &= ~SBSA_GWDT_WCS_EN;
    s->wcs &= ~SBSA_GWDT_WCS_WS0;
    s->wcs &= ~SBSA_GWDT_WCS_WS1;
    s->wcvl = 0;
    s->wcvu = 0;
    s->worl = 0;
    s->woru = 0;
    s->id = SBSA_GWDT_ID;
}

static void sbsa_gwdt_reset(void *opaque)
{
    DeviceState *sbsa_gwdt = opaque;

    wdt_sbsa_gwdt_reset(sbsa_gwdt);
}

static void sbsa_gwdt_timer_sysinterrupt(void *opaque)
{
    SBSA_GWDTState *s = SBSA_GWDT(opaque);

    s->wcs |= SBSA_GWDT_WCS_WS0;
    s->ws0 = true;
    qemu_set_irq(s->irq, 1);
    sbsa_gwdt_update_timer(s, false, true);
}

static void sbsa_gwdt_timer_sysreset(void *dev)
{
    SBSA_GWDTState *s = SBSA_GWDT(dev);
    s->wcs |= SBSA_GWDT_WCS_WS1;
    s->ws1 = true;
    qemu_log_mask(CPU_LOG_RESET, "Watchdog timer expired.\n");
    /*
     * Reset the watchdog only if the guest gets notified about
     * expiry. watchdog_perform_action() may temporarily relinquish
     * the BQL; reset before triggering the action to avoid races with
     * sbsa_gwdt instructions.
     */
    switch (get_watchdog_action()) {
    case WATCHDOG_ACTION_DEBUG:
    case WATCHDOG_ACTION_NONE:
    case WATCHDOG_ACTION_PAUSE:
        break;
    default:
        wdt_sbsa_gwdt_reset(dev);
    }
    watchdog_perform_action();
}

static const MemoryRegionOps sbsa_gwdt_rops = {
    .read = sbsa_gwdt_rread,
    .write = sbsa_gwdt_rwrite,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static const MemoryRegionOps sbsa_gwdt_ops = {
    .read = sbsa_gwdt_read,
    .write = sbsa_gwdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void wdt_sbsa_gwdt_realize(DeviceState *dev, Error **errp)
{
    SBSA_GWDTState *s = SBSA_GWDT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->rmmio, OBJECT(dev),
                          &sbsa_gwdt_rops, s,
                          "sbsa_gwdt.refresh",
                          SBSA_GWDT_RMMIO_SIZE);

    memory_region_init_io(&s->cmmio, OBJECT(dev),
                          &sbsa_gwdt_ops, s,
                          "sbsa_gwdt.control",
                          SBSA_GWDT_CMMIO_SIZE);

    sysbus_init_mmio(sbd, &s->rmmio);
    sysbus_init_mmio(sbd, &s->cmmio);

    sysbus_init_irq(sbd, &s->irq);

    qemu_register_reset(sbsa_gwdt_reset, s);

    s->ptimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sbsa_gwdt_timer_sysinterrupt,
            dev);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sbsa_gwdt_timer_sysreset,
                dev);
}

static void wdt_sbsa_gwdt_unrealize(DeviceState *dev)
{
    SBSA_GWDTState *s = SBSA_GWDT(dev);

    timer_del(s->ptimer);
    timer_free(s->ptimer);

    timer_del(s->timer);
    timer_free(s->timer);
}

static void wdt_sbsa_gwdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = wdt_sbsa_gwdt_realize;
    dc->unrealize = wdt_sbsa_gwdt_unrealize;
    dc->reset = wdt_sbsa_gwdt_reset;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->vmsd = &vmstate_sbsa_gwdt;
}

static const TypeInfo wdt_sbsa_gwdt_info = {
    .class_init = wdt_sbsa_gwdt_class_init,
    .parent = TYPE_SYS_BUS_DEVICE,
    .name  = TYPE_WDT_SBSA_GWDT,
    .instance_size  = sizeof(SBSA_GWDTState),
};

static void wdt_sbsa_gwdt_register_types(void)
{
    watchdog_add_model(&model);
    type_register_static(&wdt_sbsa_gwdt_info);
}

type_init(wdt_sbsa_gwdt_register_types)
