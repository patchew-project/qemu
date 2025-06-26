/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "system/block-backend-global-state.h"
#include "system/block-backend-io.h"
#include "hw/misc/aspeed_otpmem.h"

static uint64_t aspeed_otpmem_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedOTPMemState *s = opaque;
    uint64_t val = 0;

    memcpy(&val, s->storage + offset, size);

    return val;
}

static void aspeed_otpmem_write(void *opaque, hwaddr offset,
                                uint64_t val, unsigned size)
{
    AspeedOTPMemState *s = opaque;

    memcpy(s->storage + offset, &val, size);
}

static void aspeed_otpmem_init_storage(uint8_t *storage, uint64_t size)
{
    uint32_t *p;
    int i, num;

    num = size / sizeof(uint32_t);
    p = (uint32_t *)storage;
    for (i = 0; i < num; i++) {
        p[i] = (i % 2 == 0) ? 0x00000000 : 0xFFFFFFFF;
    }
}

static const MemoryRegionOps aspeed_otpmem_ops = {
    .read = aspeed_otpmem_read,
    .write = aspeed_otpmem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void aspeed_otpmem_realize(DeviceState *dev, Error **errp)
{
    AspeedOTPMemState *s = ASPEED_OTPMEM(dev);

    s->storage = g_malloc(s->size);

    aspeed_otpmem_init_storage(s->storage, s->size);

    memory_region_init_io(&s->mmio, OBJECT(dev), &aspeed_otpmem_ops,
                          s, "aspeed.otpmem", s->size);
    address_space_init(&s->as, &s->mmio, NULL);
}

static const Property aspeed_otpmem_properties[] = {
    DEFINE_PROP_UINT64("size", AspeedOTPMemState, size, OTPMEM_SIZE),
};

static void aspeed_otpmem_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_otpmem_realize;
    device_class_set_props(dc, aspeed_otpmem_properties);
}

static const TypeInfo aspeed_otpmem_info = {
    .name          = TYPE_ASPEED_OTPMEM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(AspeedOTPMemState),
    .class_init    = aspeed_otpmem_class_init,
};

static void aspeed_otpmem_register_types(void)
{
    type_register_static(&aspeed_otpmem_info);
}

type_init(aspeed_otpmem_register_types)
