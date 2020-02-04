/*
 *  Memexpose ARM device
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "memexpose-core.h"
#include "memexpose-memregion.h"

static void memexpose_memdev_intr(void *opaque, int dir)
{
    MemexposeMemdev *dev = opaque;
    if (dir) {
        qemu_set_irq(dev->irq, 1);
    } else {
        qemu_set_irq(dev->irq, 0);
    }
}

static int memexpose_memdev_enable(void *opaque)
{
    int ret;
    MemexposeMemdev *s = opaque;

    ret = memexpose_intr_enable(&s->intr);
    if (ret) {
        return ret;
    }

    ret = memexpose_mem_enable(&s->mem);
    if (ret) {
        memexpose_intr_disable(&s->intr);
        return ret;
    }

    return 0;
}

static void memexpose_memdev_disable(void *opaque)
{
    MemexposeMemdev *s = opaque;

    memexpose_intr_disable(&s->intr);
    memexpose_mem_disable(&s->mem);
}

static void memexpose_memdev_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MemexposeMemdev *mdev = MEMEXPOSE_MEMDEV(obj);
    sysbus_init_mmio(sbd, &mdev->intr.shmem);
    sysbus_init_irq(sbd, &mdev->irq);
}

static void memexpose_memdev_finalize(Object *obj)
{
}

static void memexpose_memdev_realize(DeviceState *dev, Error **errp)
{
    MemexposeMemdev *mdev = MEMEXPOSE_MEMDEV(dev);
    struct memexpose_intr_ops ops = {
        .parent = dev,
        .intr = memexpose_memdev_intr,
        .enable = memexpose_memdev_enable,
        .disable = memexpose_memdev_disable,
    };

    memexpose_intr_init(&mdev->intr, &ops, OBJECT(dev), &mdev->intr_chr, errp);
    if (*errp) {
        return;
    }
    memexpose_mem_init(&mdev->mem, OBJECT(dev),
                       get_system_memory(),
                       &mdev->mem_chr, 1, errp);
    if (*errp) {
        goto free_intr;
    }
    return;

free_intr:
    memexpose_intr_destroy(&mdev->intr);
}

static void memexpose_memdev_unrealize(DeviceState *dev, Error **errp)
{
    MemexposeMemdev *mdev = MEMEXPOSE_MEMDEV(dev);
    memexpose_mem_destroy(&mdev->mem);
    memexpose_intr_destroy(&mdev->intr);
}

static Property memexpose_memdev_properties[] = {
    DEFINE_PROP_CHR("intr_chardev", MemexposeMemdev, intr_chr),
    DEFINE_PROP_CHR("mem_chardev", MemexposeMemdev, mem_chr),
    DEFINE_PROP_UINT64("shm_size", MemexposeMemdev, mem.shmem_size, 4096),
    DEFINE_PROP_END_OF_LIST(),
};

static void memexpose_memdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = memexpose_memdev_realize;
    dc->unrealize = memexpose_memdev_unrealize;
    device_class_set_props(dc, memexpose_memdev_properties);
}

static const TypeInfo memexpose_memdev_info = {
    .name = TYPE_MEMEXPOSE_MEMDEV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MemexposeMemdev),
    .instance_init = memexpose_memdev_init,
    .instance_finalize = memexpose_memdev_finalize,
    .class_init = memexpose_memdev_class_init,
};

static void register_types(void)
{
    type_register_static(&memexpose_memdev_info);
}

type_init(register_types);
