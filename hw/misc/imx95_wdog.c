/*
 * NXP i.MX 95 ULP Watchdog stub model
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Minimal stub of the ULP watchdog (compatible "fsl,imx93-wdt") used
 * by U-Boot SPL's arch_cpu_init() to disable WDG3/4/5 before the
 * console comes up. SPL's disable_wdog() reads CS @ 0x00 and either
 * early-exits if the enable bit (0x80) is clear or runs an unlock +
 * disable handshake that polls for CS.ULK (0x800) and CS.RCS (0x400).
 *
 * The stub leaves the watchdog disabled at reset, so the first CS
 * read returns 0 and disable_wdog() returns immediately. The unlock
 * sequence is implemented for the cases where the guest forces the
 * unlock anyway: a write of 0xD928C520 to CNT @ 0x04 sets CS.ULK,
 * any subsequent write to CS sets CS.RCS. No timer behaviour is
 * modelled.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_IMX95_WDOG "imx95.wdog"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95WDogState, IMX95_WDOG)

#define IMX95_WDOG_REG_SIZE     0x10000

/* Register offsets. */
#define WDOG_CS                 0x00
#define WDOG_CNT                0x04
#define WDOG_TOVAL              0x08
#define WDOG_WIN                0x0C

/* CS bit fields exercised by U-Boot. */
#define CS_EN                   0x00000080
#define CS_RCS                  0x00000400  /* reconfig complete */
#define CS_ULK                  0x00000800  /* unlocked */

#define UNLOCK_WORD             0xD928C520

struct IMX95WDogState {
    SysBusDevice    parent_obj;

    MemoryRegion    iomem;

    uint32_t        cs;
    uint32_t        cnt;
    uint32_t        toval;
    uint32_t        win;
};

static uint64_t imx95_wdog_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95WDogState *s = opaque;

    switch (offset) {
    case WDOG_CS:
        return s->cs;
    case WDOG_CNT:
        return s->cnt;
    case WDOG_TOVAL:
        return s->toval;
    case WDOG_WIN:
        return s->win;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void imx95_wdog_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMX95WDogState *s = opaque;

    switch (offset) {
    case WDOG_CS:
        /*
         * SPL writes CS to update timeout + window and to clear EN.
         * Acknowledge by setting RCS (reconfig complete) so the
         * "wait for RCS" loop at the tail of disable_wdog() exits.
         */
        s->cs = (value & ~CS_RCS) | CS_RCS;
        trace_imx95_wdog_config(s->cs);
        break;

    case WDOG_CNT:
        s->cnt = value;
        /*
         * A 32-bit UNLOCK_WORD write to CNT puts the watchdog into
         * the unlocked state. Any other value (e.g. REFRESH_WORD)
         * is just a refresh ping; ignore.
         */
        if ((uint32_t)value == UNLOCK_WORD) {
            s->cs |= CS_ULK;
            trace_imx95_wdog_unlock();
        }
        break;

    case WDOG_TOVAL:
        s->toval = value;
        break;

    case WDOG_WIN:
        s->win = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps imx95_wdog_ops = {
    .read = imx95_wdog_read,
    .write = imx95_wdog_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_wdog_reset_hold(Object *obj, ResetType type)
{
    IMX95WDogState *s = IMX95_WDOG(obj);

    /* Watchdog disabled at reset: CS = 0 makes disable_wdog() early-exit. */
    s->cs = 0;
    s->cnt = 0;
    s->toval = 0x400;
    s->win = 0;
}

static void imx95_wdog_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95WDogState *s = IMX95_WDOG(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_wdog_ops, s,
                          TYPE_IMX95_WDOG, IMX95_WDOG_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_imx95_wdog = {
    .name = TYPE_IMX95_WDOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cs, IMX95WDogState),
        VMSTATE_UINT32(cnt, IMX95WDogState),
        VMSTATE_UINT32(toval, IMX95WDogState),
        VMSTATE_UINT32(win, IMX95WDogState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_wdog_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx95_wdog;
    rc->phases.hold = imx95_wdog_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 ULP watchdog (stub)";
}

static const TypeInfo imx95_wdog_info = {
    .name           = TYPE_IMX95_WDOG,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95WDogState),
    .instance_init  = imx95_wdog_init,
    .class_init     = imx95_wdog_class_init,
};

static void imx95_wdog_register_types(void)
{
    type_register_static(&imx95_wdog_info);
}

type_init(imx95_wdog_register_types)
