/*
 * ASPEED Watchdog Controller
 *
 * Copyright (C) 2016-2017 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "sysemu/watchdog.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/watchdog/wdt_aspeed.h"

#define WDT_IO_REGION_SIZE      0x20

#define WDT_STATUS              0x00
#define WDT_RELOAD_VALUE        0x04
#define WDT_RESTART             0x08
#define WDT_CTRL                0x0C
#define WDT_TIMEOUT_STATUS      0x10
#define WDT_TIMEOUT_CLEAR       0x14
#define WDT_RESET_WDITH         0x18

#define WDT_RESTART_MAGIC       0x4755

static uint64_t aspeed_wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedWDTState *s = ASPEED_WDT(opaque);

    switch (offset) {
    case WDT_STATUS:
        return s->reg_status;
    case WDT_RELOAD_VALUE:
        return s->reg_reload_value;
    case WDT_RESTART:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from write-only reg at offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        return 0;
    case WDT_CTRL:
        return s->reg_ctrl;
    case WDT_TIMEOUT_STATUS:
    case WDT_TIMEOUT_CLEAR:
    case WDT_RESET_WDITH:
        qemu_log_mask(LOG_UNIMP,
                      "%s: uninmplemented read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

}

#define PCLK_HZ 24000000

static void aspeed_wdt_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedWDTState *s = ASPEED_WDT(opaque);
    bool en = data & BIT(0);
    bool pclk = !(data & BIT(4));

    switch (offset) {
    case WDT_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only reg at offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        break;
    case WDT_RELOAD_VALUE:
        s->reg_reload_value = data;
        break;
    case WDT_RESTART:
        if ((data & 0xFFFF) == 0x4755) {
            uint32_t reload;

            s->reg_status = s->reg_reload_value;

            if (pclk) {
                reload = muldiv64(s->reg_reload_value, NANOSECONDS_PER_SECOND,
                                  PCLK_HZ) ;
            } else {
                reload = s->reg_reload_value * 1000;
            }

            if (s->enabled) {
                timer_mod(s->timer,
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + reload);
            }
        }
        break;
    case WDT_CTRL:
        if (en && !s->enabled) {
            uint32_t reload;

            if (pclk) {
                reload = muldiv64(s->reg_reload_value, NANOSECONDS_PER_SECOND,
                                  PCLK_HZ);
            } else {
                reload = s->reg_reload_value * 1000;
            }

            s->enabled = true;
            timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + reload);
        } else if (!en && s->enabled) {
            s->enabled = false;
            timer_del(s->timer);
        }
        break;
    case WDT_TIMEOUT_STATUS:
    case WDT_TIMEOUT_CLEAR:
    case WDT_RESET_WDITH:
        qemu_log_mask(LOG_UNIMP,
                      "%s: uninmplemented write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
    return;
}

static WatchdogTimerModel model = {
    .wdt_name = TYPE_ASPEED_WDT,
    .wdt_description = "aspeed watchdog device",
};

static const VMStateDescription vmstate_aspeed_wdt = {
    .name = "vmstate_aspeed_wdt",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, AspeedWDTState),
        VMSTATE_BOOL(enabled, AspeedWDTState),
        VMSTATE_END_OF_LIST()
    }
};

static const MemoryRegionOps aspeed_wdt_ops = {
    .read = aspeed_wdt_read,
    .write = aspeed_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_wdt_reset(DeviceState *dev)
{
    AspeedWDTState *s = ASPEED_WDT(dev);

    s->reg_status = 0x3EF1480;
    s->reg_reload_value = 0x03EF1480;
    s->reg_restart = 0;
    s->reg_ctrl = 0;

    s->enabled = false;
    timer_del(s->timer);
}

static void aspeed_wdt_timer_expired(void *dev)
{
    AspeedWDTState *s = ASPEED_WDT(dev);

    qemu_log_mask(CPU_LOG_RESET, "Watchdog timer expired.\n");
    watchdog_perform_action();
    timer_del(s->timer);
}

static void aspeed_wdt_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedWDTState *s = ASPEED_WDT(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, aspeed_wdt_timer_expired,
                            dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_wdt_ops, s,
                          TYPE_ASPEED_WDT, WDT_IO_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_wdt_realize;
    dc->reset = aspeed_wdt_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->vmsd = &vmstate_aspeed_wdt;
}

static const TypeInfo aspeed_wdt_info = {
    .parent = TYPE_SYS_BUS_DEVICE,
    .name  = TYPE_ASPEED_WDT,
    .instance_size  = sizeof(AspeedWDTState),
    .class_init = aspeed_wdt_class_init,
};

static void wdt_aspeed_register_types(void)
{
    watchdog_add_model(&model);
    type_register_static(&aspeed_wdt_info);
}

type_init(wdt_aspeed_register_types)
