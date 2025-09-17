/*
 * ASPEED LTPI Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/misc/aspeed_ltpi.h"

#define LTPI_LINK_MNG 0x42
#define LTPI_PHY_MODE 0x80

static uint64_t ltpi_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    if (idx >= ASPEED_LTPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad offset=0x%" HWADDR_PRIx "\n",
                      TYPE_ASPEED_LTPI, offset);
        return 0;
    }

    return s->regs[idx];
}

static void ltpi_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    AspeedLTPIState *s = opaque;
    uint32_t idx = offset >> 2;

    if (idx >= ASPEED_LTPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad offset=0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n",
                      TYPE_ASPEED_LTPI, offset, val);
        return;
    }

    switch (offset) {
    default:
        s->regs[idx] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps ltpi_ops = {
    .read = ltpi_read,
    .write = ltpi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void ltpi_reset(DeviceState *dev)
{
    AspeedLTPIState *s = ASPEED_LTPI(dev);
    memset(s->regs, 0, sizeof(s->regs));
    /* set default values */
    s->regs[LTPI_LINK_MNG] = 0x11900007;
    s->regs[LTPI_PHY_MODE] = 0x2;
}


static const VMStateDescription vmstate_ltpi = {
    .name = TYPE_ASPEED_LTPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedLTPIState,
                             ASPEED_LTPI_NR_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void ltpi_realize(DeviceState *dev, Error **errp)
{
    AspeedLTPIState *s = ASPEED_LTPI(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &ltpi_ops, s,
                          TYPE_ASPEED_LTPI, ASPEED_LTPI_NR_REGS);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void ltpi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = ltpi_realize;
    dc->vmsd = &vmstate_ltpi;
    device_class_set_legacy_reset(dc, ltpi_reset);
}

static const TypeInfo ltpi_info = {
    .name          = TYPE_ASPEED_LTPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedLTPIState),
    .class_init    = ltpi_class_init,
};

static void ltpi_register_types(void)
{
    type_register_static(&ltpi_info);
}

type_init(ltpi_register_types);
