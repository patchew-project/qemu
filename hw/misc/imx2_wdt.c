/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/hw.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/watchdog.h"
#include "qemu/error-report.h"
#include "qemu/sizes.h"

#include "hw/misc/imx2_wdt.h"


#define IMX2_WDT_WCR_WDA	BIT(5)		/* -> External Reset WDOG_B */
#define IMX2_WDT_WCR_SRS	BIT(4)		/* -> Software Reset Signal */

static uint64_t imx2_wdt_read(void *opaque, hwaddr addr,
                              unsigned int size)
{
    IMX2WdtState *s = opaque;
    const size_t index = addr / sizeof(s->reg[0]);

    if (index < ARRAY_SIZE(s->reg))
        return s->reg[index];
    else
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);

    return 0xDEADBEEF;
}

static void imx2_wdt_write(void *opaque, hwaddr addr,
                           uint64_t val64, unsigned int size)
{
    uint16_t value  = val64;
    IMX2WdtState *s = opaque;
    const size_t index = addr / sizeof(s->reg[0]);

    switch (index) {
    case IMX2_WDT_WCR:
        if (value & (IMX2_WDT_WCR_WDA | IMX2_WDT_WCR_SRS))
            watchdog_perform_action();
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps imx2_wdt_ops = {
    .read  = imx2_wdt_read,
    .write = imx2_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx2_wdt_realize(DeviceState *dev, Error **errp)
{
    IMX2WdtState *s = IMX2_WDT(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &imx2_wdt_ops, s,
                          TYPE_IMX2_WDT".mmio", SZ_64K);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void imx2_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx2_wdt_realize;

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
