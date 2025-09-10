/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson  Advance interrupt controller.
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/intc/loongarch_avec.h"
#include "hw/pci/msi.h"
#include "hw/misc/unimp.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/qdev-properties.h"

static uint64_t loongarch_avec_mem_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    return 0;
}

static void loongarch_avec_mem_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    return;
}


static const MemoryRegionOps loongarch_avec_ops = {
    .read = loongarch_avec_mem_read,
    .write = loongarch_avec_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_avec_realize(DeviceState *dev, Error **errp)
{
    LoongArchAVECClass *lac = LOONGARCH_AVEC_GET_CLASS(dev);

    Error *local_err = NULL;
    lac->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    return;
}

static void loongarch_avec_unrealize(DeviceState *dev)
{
    return;
}

static void loongarch_avec_init(Object *obj)
{
    LoongArchAVECState *s = LOONGARCH_AVEC(obj);
    SysBusDevice *shd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->avec_mmio, OBJECT(s), &loongarch_avec_ops,
                          s, TYPE_LOONGARCH_AVEC, VIRT_AVEC_MSG_OFFSET);
    sysbus_init_mmio(shd, &s->avec_mmio);
    msi_nonbroken = true;
    return;
}

static void loongarch_avec_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchAVECClass *lac = LOONGARCH_AVEC_CLASS(klass);

    dc->unrealize = loongarch_avec_unrealize;
    device_class_set_parent_realize(dc, loongarch_avec_realize,
                                    &lac->parent_realize);
}

static const TypeInfo loongarch_avec_info = {
    .name          = TYPE_LOONGARCH_AVEC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchAVECState),
    .instance_init = loongarch_avec_init,
    .class_init    = loongarch_avec_class_init,
};

static void loongarch_avec_register_types(void)
{
    type_register_static(&loongarch_avec_info);
}

type_init(loongarch_avec_register_types)
