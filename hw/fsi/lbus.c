/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Local bus where FSI slaves are connected
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/fsi/lbus.h"
#include "hw/core/qdev-properties.h"
#include "qemu/log.h"
#include "trace.h"

#define TO_REG(offset) ((offset) >> 2)

static void fsi_lbus_init(Object *o)
{
    FSILBus *lbus = FSI_LBUS(o);

    memory_region_init(&lbus->mr, OBJECT(lbus), TYPE_FSI_LBUS, 1 * MiB);
}

static const TypeInfo fsi_lbus_info = {
    .name = TYPE_FSI_LBUS,
    .parent = TYPE_BUS,
    .instance_init = fsi_lbus_init,
    .instance_size = sizeof(FSILBus),
};

static const TypeInfo fsi_lbus_device_type_info = {
    .name = TYPE_FSI_LBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FSILBusDevice),
    .abstract = true,
};

static uint64_t fsi_scratchpad_read(void *opaque, hwaddr addr, unsigned size)
{
    FSIScratchPad *s = SCRATCHPAD(opaque);
    int reg = TO_REG(addr);

    trace_fsi_scratchpad_read(addr, size);

    if (reg >= FSI_SCRATCHPAD_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    return s->regs[reg];
}

static void fsi_scratchpad_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    FSIScratchPad *s = SCRATCHPAD(opaque);

    trace_fsi_scratchpad_write(addr, size, data);
    int reg = TO_REG(addr);

    if (reg >= FSI_SCRATCHPAD_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    s->regs[reg] = data;
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

    memset(s->regs, 0, sizeof(s->regs));
}

static void fsi_scratchpad_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_LBUS;
    dc->realize = fsi_scratchpad_realize;
    device_class_set_legacy_reset(dc, fsi_scratchpad_reset);
}

static const TypeInfo fsi_scratchpad_info = {
    .name = TYPE_FSI_SCRATCHPAD,
    .parent = TYPE_FSI_LBUS_DEVICE,
    .instance_size = sizeof(FSIScratchPad),
    .class_init = fsi_scratchpad_class_init,
};

/* The mailbox exposes its scratch registers at this offset */
#define FSI_MBOX_SCRATCH_OFF 0xe0
#define FSI_MBOX_SCRATCH_END (FSI_MBOX_SCRATCH_OFF + FSI_MBOX_SCRATCH_NUM * 4)

static uint64_t fsi_mbox_read(void *opaque, hwaddr addr, unsigned size)
{
    FSIMbox *mbox = FSI_MBOX(opaque);

    trace_fsi_mbox_read(addr, size);

    if (addr < FSI_MBOX_SCRATCH_OFF || addr >= FSI_MBOX_SCRATCH_END) {
        return 0;
    }

    return mbox->scratch[TO_REG(addr - FSI_MBOX_SCRATCH_OFF)];
}

static void fsi_mbox_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    FSIMbox *mbox = FSI_MBOX(opaque);

    trace_fsi_mbox_write(addr, size, data);

    if (addr < FSI_MBOX_SCRATCH_OFF || addr >= FSI_MBOX_SCRATCH_END) {
        return;
    }

    mbox->scratch[TO_REG(addr - FSI_MBOX_SCRATCH_OFF)] = data;
}

static const struct MemoryRegionOps fsi_mbox_ops = {
    .read = fsi_mbox_read,
    .write = fsi_mbox_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void fsi_mbox_realize(DeviceState *dev, Error **errp)
{
    FSILBusDevice *ldev = FSI_LBUS_DEVICE(dev);

    memory_region_init_io(&ldev->iomem, OBJECT(ldev), &fsi_mbox_ops,
                          ldev, TYPE_FSI_MBOX, 0x400);
}

static void fsi_mbox_reset(DeviceState *dev)
{
    FSIMbox *mbox = FSI_MBOX(dev);

    memset(mbox->scratch, 0, sizeof(mbox->scratch));
}

static void fsi_mbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_LBUS;
    dc->realize = fsi_mbox_realize;
    device_class_set_legacy_reset(dc, fsi_mbox_reset);
}

static const TypeInfo fsi_mbox_info = {
    .name = TYPE_FSI_MBOX,
    .parent = TYPE_FSI_LBUS_DEVICE,
    .instance_size = sizeof(FSIMbox),
    .class_init = fsi_mbox_class_init,
};

static void fsi_lbus_register_types(void)
{
    type_register_static(&fsi_lbus_info);
    type_register_static(&fsi_lbus_device_type_info);
    type_register_static(&fsi_scratchpad_info);
    type_register_static(&fsi_mbox_info);
}

type_init(fsi_lbus_register_types);
