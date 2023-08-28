/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM scratchpad engine
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/fsi/engine-scratchpad.h"

static uint64_t scratchpad_read(void *opaque, hwaddr addr, unsigned size)
{
    ScratchPad *s = SCRATCHPAD(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: read @0x%" HWADDR_PRIx " size=%d\n",
                  __func__, addr, size);

    if (addr) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->reg;
}

static void scratchpad_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    ScratchPad *s = SCRATCHPAD(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: write @0x%" HWADDR_PRIx " size=%d "
                  "value=%"PRIx64"\n", __func__, addr, size, data);

    if (addr) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    s->reg = data;
}

static const struct MemoryRegionOps scratchpad_ops = {
    .read = scratchpad_read,
    .write = scratchpad_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void scratchpad_realize(DeviceState *dev, Error **errp)
{
    LBusDevice *ldev = LBUS_DEVICE(dev);

    memory_region_init_io(&ldev->iomem, OBJECT(ldev), &scratchpad_ops,
                          ldev, TYPE_SCRATCHPAD, 0x400);
}

static void scratchpad_reset(DeviceState *dev)
{
    ScratchPad *s = SCRATCHPAD(dev);

    s->reg = 0;
}

static void scratchpad_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LBusDeviceClass *ldc = LBUS_DEVICE_CLASS(klass);

    dc->realize = scratchpad_realize;
    dc->reset = scratchpad_reset;

    ldc->config =
          ENGINE_CONFIG_NEXT            /* valid */
        | 0x00010000                    /* slots */
        | 0x00001000                    /* version */
        | ENGINE_CONFIG_TYPE_SCRATCHPAD /* type */
        | 0x00000007;                   /* crc */
}

static const TypeInfo scratchpad_info = {
    .name = TYPE_SCRATCHPAD,
    .parent = TYPE_LBUS_DEVICE,
    .instance_size = sizeof(ScratchPad),
    .class_init = scratchpad_class_init,
    .class_size = sizeof(LBusDeviceClass),
};

static void scratchpad_register_types(void)
{
    type_register_static(&scratchpad_info);
}

type_init(scratchpad_register_types);
