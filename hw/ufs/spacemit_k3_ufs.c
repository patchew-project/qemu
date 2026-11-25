/*
 * Spacemit K3 UFS Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "hw/ufs/spacemit_k3_ufs.h"
#include "system/memory.h"
#include "trace.h"

#define K3_UFS_VENDOR_BASE 0x1b00
#define K3_UFS_VENDOR_SIZE 0x500

#define K3_UFS_TOTAL_SIZE 0x2000

#define K3_UFS_MPHY_PU_PLL_LOCK BIT(31)

#define K3_UFS_MPHY_PU_CTRL 0x4

static uint64_t spacemit_k3_ufs_vendor_read(void *opaque, hwaddr addr,
                                              unsigned size)
{
    uint32_t ret = 0;

    switch (addr) {
    case K3_UFS_MPHY_PU_CTRL:
        ret = K3_UFS_MPHY_PU_PLL_LOCK;
        break;
    }

    trace_spacemit_k3_ufs_vendor_read(addr, size, ret);
    return ret;
}

static void spacemit_k3_ufs_vendor_write(void *opaque, hwaddr addr,
                                           uint64_t value, unsigned size)
{
    trace_spacemit_k3_ufs_vendor_write(addr, size, value);
}

static const MemoryRegionOps spacemit_k3_ufs_vendor_ops = {
    .read = spacemit_k3_ufs_vendor_read,
    .write = spacemit_k3_ufs_vendor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

static void spacemit_k3_ufs_realize(DeviceState *dev, Error **errp)
{
    SpacemitK3UFSState *s = SPACEMIT_K3_UFS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *ufs_sbd = SYS_BUS_DEVICE(&s->ufs);

    memory_region_init(&s->container, OBJECT(s), "k3.ufs",
                       K3_UFS_VENDOR_BASE + K3_UFS_VENDOR_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    sysbus_realize(ufs_sbd, errp);
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(ufs_sbd, 0));

    sysbus_pass_irq(sbd, ufs_sbd);
    s->bus = BUS(&s->ufs.ufs.bus);

    memory_region_init_io(
        &s->vendor_mem, OBJECT(s), &spacemit_k3_ufs_vendor_ops, s,
        "k3.ufs.vendor", K3_UFS_VENDOR_SIZE);
    memory_region_add_subregion_overlap(&s->container, K3_UFS_VENDOR_BASE,
                                &s->vendor_mem, 1);
}

static void spacemit_k3_ufs_instance_init(Object *obj)
{

    SpacemitK3UFSState *s = SPACEMIT_K3_UFS(obj);

    object_initialize_child(obj, "ufs", &s->ufs, TYPE_SYSBUS_UFS);
}

static void spacemit_k3_ufs_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = spacemit_k3_ufs_realize;
}

static const TypeInfo spacemit_k3_ufs_info = {
    .name = TYPE_SPACEMIT_K3_UFS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpacemitK3UFSState),
    .instance_init = spacemit_k3_ufs_instance_init,
    .class_init = spacemit_k3_ufs_class_init,
};

static void spacemit_k3_ufs_register_types(void)
{
    type_register_static(&spacemit_k3_ufs_info);
}

type_init(spacemit_k3_ufs_register_types)
