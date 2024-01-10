/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Local bus where FSI slaves are connected
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/fsi/lbus.h"

#include "hw/qdev-properties.h"

#include "trace.h"

static void lbus_init(Object *o)
{
    FSILBus *lbus = FSI_LBUS(o);

    memory_region_init(&lbus->mr, OBJECT(lbus), TYPE_FSI_LBUS,
                       FSI_LBUS_MEM_REGION_SIZE - FSI_LBUSDEV_IOMEM_START);
}

static const TypeInfo lbus_info = {
    .name = TYPE_FSI_LBUS,
    .parent = TYPE_BUS,
    .instance_init = lbus_init,
    .instance_size = sizeof(FSILBus),
};

static void lbus_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_LBUS;
}

static const TypeInfo lbus_device_type_info = {
    .name = TYPE_FSI_LBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FSILBusDevice),
    .abstract = true,
    .class_init = lbus_device_class_init,
    .class_size = sizeof(FSILBusDeviceClass),
};

static uint64_t fsi_scratchpad_read(void *opaque, hwaddr addr, unsigned size)
{
    FSIScratchPad *s = SCRATCHPAD(opaque);

    trace_fsi_scratchpad_read(addr, size);

    if (addr & ~(FSI_SCRATCHPAD_NR_REGS - 1)) {
        return 0;
    }

    return s->reg[addr];
}

static void fsi_scratchpad_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    FSIScratchPad *s = SCRATCHPAD(opaque);

    trace_fsi_scratchpad_write(addr, size, data);

    if (addr & ~(FSI_SCRATCHPAD_NR_REGS - 1)) {
        return;
    }

    s->reg[addr] = data;
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
    int i;

    for (i = 0; i < FSI_SCRATCHPAD_NR_REGS; i++) {
        s->reg[i] = 0;
    }
}

static void fsi_scratchpad_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = fsi_scratchpad_realize;
    dc->reset = fsi_scratchpad_reset;
}

static const TypeInfo fsi_scratchpad_info = {
    .name = TYPE_FSI_SCRATCHPAD,
    .parent = TYPE_FSI_LBUS_DEVICE,
    .instance_size = sizeof(FSIScratchPad),
    .class_init = fsi_scratchpad_class_init,
    .class_size = sizeof(FSILBusDeviceClass),
};

static void lbus_register_types(void)
{
    type_register_static(&lbus_info);
    type_register_static(&lbus_device_type_info);
    type_register_static(&fsi_scratchpad_info);
}

type_init(lbus_register_types);
