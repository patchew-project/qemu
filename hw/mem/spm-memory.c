/*
 * Specific Purpose Memory (SPM) device
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * Authors:
 *  FangSheng Huang <FangSheng.Huang@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "hw/mem/spm-memory.h"
#include "hw/mem/memory-device.h"
#include "migration/vmstate.h"
#include "system/hostmem.h"

#define SPM_MEMORY_MEMDEV_PROP    "memdev"
#define SPM_MEMORY_NODE_PROP      "node"
#define SPM_MEMORY_ADDR_PROP      "addr"

static const Property spm_memory_properties[] = {
    DEFINE_PROP_LINK(SPM_MEMORY_MEMDEV_PROP, SpmMemoryDevice, hostmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_UINT32(SPM_MEMORY_NODE_PROP, SpmMemoryDevice, node, 0),
    DEFINE_PROP_UINT64(SPM_MEMORY_ADDR_PROP, SpmMemoryDevice, addr, 0),
};

static uint64_t spm_memory_md_get_addr(const MemoryDeviceState *md)
{
    return SPM_MEMORY(md)->addr;
}

static void spm_memory_md_set_addr(MemoryDeviceState *md, uint64_t addr,
                                   Error **errp)
{
    SPM_MEMORY(md)->addr = addr;
}

static MemoryRegion *spm_memory_md_get_memory_region(MemoryDeviceState *md,
                                                    Error **errp)
{
    SpmMemoryDevice *spm = SPM_MEMORY(md);

    if (!spm->hostmem) {
        error_setg(errp, "'memdev' property must be set");
        return NULL;
    }
    return host_memory_backend_get_memory(spm->hostmem);
}

static uint64_t spm_memory_md_get_plugged_size(const MemoryDeviceState *md,
                                               Error **errp)
{
    SpmMemoryDevice *spm = SPM_MEMORY(md);
    return spm->hostmem ?
        memory_region_size(host_memory_backend_get_memory(spm->hostmem)) : 0;
}

static void spm_memory_md_fill_device_info(const MemoryDeviceState *md,
                                           MemoryDeviceInfo *info)
{
    SpmMemoryDeviceInfo *di = g_new0(SpmMemoryDeviceInfo, 1);
    SpmMemoryDevice *spm = SPM_MEMORY(md);
    DeviceState *dev = DEVICE(md);

    di->id = dev->id ? g_strdup(dev->id) : NULL;
    di->memaddr = spm->addr;
    di->size = spm->hostmem ? memory_region_size(
                   host_memory_backend_get_memory(spm->hostmem)) : 0;
    di->node = spm->node;
    di->memdev = spm->hostmem ?
                 object_get_canonical_path(OBJECT(spm->hostmem)) : NULL;

    info->u.spm_memory.data = di;
    info->type = MEMORY_DEVICE_INFO_KIND_SPM_MEMORY;
}

static void spm_memory_realize(DeviceState *dev, Error **errp)
{
    SpmMemoryDevice *spm = SPM_MEMORY(dev);

    if (!spm->hostmem) {
        error_setg(errp, "'%s' property is required", SPM_MEMORY_MEMDEV_PROP);
        return;
    }
}

static const VMStateDescription vmstate_spm_memory = {
    .name = TYPE_SPM_MEMORY,
    .unmigratable = 1,
};

static void spm_memory_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(oc);

    dc->desc = "SPM (Specific Purpose Memory) device";
    dc->hotpluggable = false;
    dc->realize = spm_memory_realize;
    dc->vmsd = &vmstate_spm_memory;
    device_class_set_props(dc, spm_memory_properties);

    mdc->get_addr            = spm_memory_md_get_addr;
    mdc->set_addr            = spm_memory_md_set_addr;
    mdc->get_memory_region   = spm_memory_md_get_memory_region;
    mdc->get_plugged_size    = spm_memory_md_get_plugged_size;
    mdc->fill_device_info    = spm_memory_md_fill_device_info;
}

static const TypeInfo spm_memory_info = {
    .name          = TYPE_SPM_MEMORY,
    .parent        = TYPE_DEVICE,
    .class_size    = sizeof(SpmMemoryDeviceClass),
    .class_init    = spm_memory_class_init,
    .instance_size = sizeof(SpmMemoryDevice),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void spm_memory_register_types(void)
{
    type_register_static(&spm_memory_info);
}

type_init(spm_memory_register_types)
