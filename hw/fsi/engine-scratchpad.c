/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM scratchpad engine
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"

#include "hw/fsi/engine-scratchpad.h"

static uint64_t fsi_scratchpad_read(void *opaque, hwaddr addr, unsigned size)
{
    FSIScratchPad *s = SCRATCHPAD(opaque);

    trace_fsi_scratchpad_read(addr, size);

    if (addr) {
        return 0;
    }

    return s->reg;
}

static void fsi_scratchpad_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    FSIScratchPad *s = SCRATCHPAD(opaque);

    trace_fsi_scratchpad_write(addr, size, data);

    if (addr) {
        return;
    }

    s->reg = data;
}

static const struct MemoryRegionOps scratchpad_ops = {
    .read = fsi_scratchpad_read,
    .write = fsi_scratchpad_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void fsi_scratchpad_realize(DeviceState *dev, Error **errp)
{
    FSILBusDevice *ldev = FSI_LBUS_DEVICE(dev);

    memory_region_init_io(&ldev->iomem, OBJECT(ldev), &scratchpad_ops,
                          ldev, TYPE_FSI_SCRATCHPAD, 0x400);
}

static void fsi_scratchpad_reset(DeviceState *dev)
{
    FSIScratchPad *s = SCRATCHPAD(dev);

    s->reg = 0;
}

static void fsi_scratchpad_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FSILBusDeviceClass *ldc = FSI_LBUS_DEVICE_CLASS(klass);

    dc->realize = fsi_scratchpad_realize;
    dc->reset = fsi_scratchpad_reset;

    ldc->config =
          ENGINE_CONFIG_NEXT            | /* valid */
          0x00010000                    | /* slots */
          0x00001000                    | /* version */
          ENGINE_CONFIG_TYPE_SCRATCHPAD | /* type */
          0x00000007;                     /* crc */
}

static const TypeInfo fsi_scratchpad_info = {
    .name = TYPE_FSI_SCRATCHPAD,
    .parent = TYPE_FSI_LBUS_DEVICE,
    .instance_size = sizeof(FSIScratchPad),
    .class_init = fsi_scratchpad_class_init,
    .class_size = sizeof(FSILBusDeviceClass),
};

static void fsi_scratchpad_register_types(void)
{
    type_register_static(&fsi_scratchpad_info);
}

type_init(fsi_scratchpad_register_types);
