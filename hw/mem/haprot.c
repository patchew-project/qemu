/*
 * A device for memory hot-add protocols
 *
 * Copyright (C) 2020 Oracle and/or its affiliates.
 *
 * Author: Maciej S. Szmigiero <maciej.szmigiero@oracle.com>
 *
 * Heavily based on pc-dimm.c:
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2014 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/boards.h"
#include "hw/mem/haprot.h"
#include "hw/mem/memory-device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "sysemu/hostmem.h"
#include "trace.h"

static Property haprot_properties[] = {
    DEFINE_PROP_UINT64(HAPROT_ADDR_PROP, HAProtDevice, addr, 0),
    DEFINE_PROP_UINT32(HAPROT_NODE_PROP, HAProtDevice, node, 0),
    DEFINE_PROP_LINK(HAPROT_MEMDEV_PROP, HAProtDevice, hostmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void haprot_get_size(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Error *local_err = NULL;
    uint64_t value;

    value = memory_device_get_region_size(MEMORY_DEVICE(obj), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    visit_type_uint64(v, name, &value, errp);
}

static void haprot_init(Object *obj)
{
    object_property_add(obj, HAPROT_SIZE_PROP, "uint64", haprot_get_size,
                        NULL, NULL, NULL);
}

static void haprot_realize(DeviceState *dev, Error **errp)
{
    HAProtDevice *haprot = HAPROT(dev);
    HAProtDeviceClass *hc = HAPROT_GET_CLASS(haprot);
    uint64_t align;
    MachineState *ms = MACHINE(qdev_get_machine());
    Error *local_err = NULL;
    int nb_numa_nodes = ms->numa_state->num_nodes;

    if (!hc->plug_notify_cb) {
        error_setg(errp, "no mem hot add protocol registered");
        return;
    }

    if (hc->get_align_cb) {
        align = hc->get_align_cb(hc->notify_cb_ctx, haprot);
    } else {
        align = 0;
    }

    memory_device_pre_plug(MEMORY_DEVICE(haprot), ms,
                           align ? &align : NULL,
                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!haprot->hostmem) {
        error_setg(errp, "'" HAPROT_MEMDEV_PROP "' property is not set");
        return;
    } else if (host_memory_backend_is_mapped(haprot->hostmem)) {
        const char *path;

        path = object_get_canonical_path_component(OBJECT(haprot->hostmem));
        error_setg(errp, "can't use already busy memdev: %s", path);
        return;
    }
    if (((nb_numa_nodes > 0) && (haprot->node >= nb_numa_nodes)) ||
        (!nb_numa_nodes && haprot->node)) {
        error_setg(errp,
                   "Node property value %"PRIu32" exceeds the number of numa nodes %d",
                   haprot->node, nb_numa_nodes ? nb_numa_nodes : 1);
        return;
    }

    host_memory_backend_set_mapped(haprot->hostmem, true);

    memory_device_plug(MEMORY_DEVICE(haprot), ms);
    vmstate_register_ram(host_memory_backend_get_memory(haprot->hostmem),
                         dev);

    hc->plug_notify_cb(hc->notify_cb_ctx, haprot, &local_err);
    if (local_err) {
        memory_device_unplug(MEMORY_DEVICE(haprot), ms);
        vmstate_unregister_ram(host_memory_backend_get_memory(haprot->hostmem),
                               dev);
        host_memory_backend_set_mapped(haprot->hostmem, false);

        error_propagate(errp, local_err);
        return;
    }
}

static void haprot_unrealize(DeviceState *dev)
{
    HAProtDevice *haprot = HAPROT(dev);
    HAProtDeviceClass *hc = HAPROT_GET_CLASS(haprot);
    MachineState *ms = MACHINE(qdev_get_machine());

    if (hc->unplug_notify_cb) {
        hc->unplug_notify_cb(hc->notify_cb_ctx, haprot);
    }

    memory_device_unplug(MEMORY_DEVICE(haprot), ms);
    vmstate_unregister_ram(host_memory_backend_get_memory(haprot->hostmem),
                           dev);

    host_memory_backend_set_mapped(haprot->hostmem, false);
}

static uint64_t haprot_md_get_addr(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), HAPROT_ADDR_PROP,
                                    &error_abort);
}

static void haprot_md_set_addr(MemoryDeviceState *md, uint64_t addr,
                               Error **errp)
{
    object_property_set_uint(OBJECT(md), HAPROT_ADDR_PROP, addr, errp);
}

static MemoryRegion *haprot_md_get_memory_region(MemoryDeviceState *md,
                                                 Error **errp)
{
    HAProtDevice *haprot = HAPROT(md);

    if (!haprot->hostmem) {
        error_setg(errp, "'" HAPROT_MEMDEV_PROP "' property must be set");
        return NULL;
    }

    return host_memory_backend_get_memory(haprot->hostmem);
}

static void haprot_md_fill_device_info(const MemoryDeviceState *md,
                                       MemoryDeviceInfo *info)
{
    PCDIMMDeviceInfo *di = g_new0(PCDIMMDeviceInfo, 1);
    const DeviceClass *dc = DEVICE_GET_CLASS(md);
    const HAProtDevice *haprot = HAPROT(md);
    const DeviceState *dev = DEVICE(md);

    if (dev->id) {
        di->has_id = true;
        di->id = g_strdup(dev->id);
    }
    di->hotplugged = dev->hotplugged;
    di->hotpluggable = dc->hotpluggable;
    di->addr = haprot->addr;
    di->slot = -1;
    di->node = haprot->node;
    di->size = object_property_get_uint(OBJECT(haprot), HAPROT_SIZE_PROP,
                                        NULL);
    di->memdev = object_get_canonical_path(OBJECT(haprot->hostmem));

    info->u.dimm.data = di;
    info->type = MEMORY_DEVICE_INFO_KIND_DIMM;
}

static void haprot_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(oc);

    dc->realize = haprot_realize;
    dc->unrealize = haprot_unrealize;
    device_class_set_props(dc, haprot_properties);
    dc->desc = "Memory for a hot add protocol";

    mdc->get_addr = haprot_md_get_addr;
    mdc->set_addr = haprot_md_set_addr;
    mdc->get_plugged_size = memory_device_get_region_size;
    mdc->get_memory_region = haprot_md_get_memory_region;
    mdc->fill_device_info = haprot_md_fill_device_info;
}

void haprot_register_protocol(HAProtocolGetAlign get_align_cb,
                              HAProtocolPlugNotify plug_notify_cb,
                              HAProtocolUnplugNotify unplug_notify_cb,
                              void *notify_ctx, Error **errp)
{
    HAProtDeviceClass *hc = HAPROT_CLASS(object_class_by_name(TYPE_HAPROT));

    if (hc->plug_notify_cb) {
        error_setg(errp, "a mem hot add protocol was already registered");
        return;
    }

    hc->get_align_cb = get_align_cb;
    hc->plug_notify_cb = plug_notify_cb;
    hc->unplug_notify_cb = unplug_notify_cb;
    hc->notify_cb_ctx = notify_ctx;
}

void haprot_unregister_protocol(HAProtocolPlugNotify plug_notify_cb,
                                Error **errp)
{
    HAProtDeviceClass *hc = HAPROT_CLASS(object_class_by_name(TYPE_HAPROT));

    if (!hc->plug_notify_cb) {
        error_setg(errp, "no mem hot add protocol was registered");
        return;
    }

    if (hc->plug_notify_cb != plug_notify_cb) {
        error_setg(errp, "different mem hot add protocol was registered");
        return;
    }

    hc->get_align_cb = NULL;
    hc->plug_notify_cb = NULL;
    hc->unplug_notify_cb = NULL;
    hc->notify_cb_ctx = NULL;
}

static TypeInfo haprot_info = {
    .name          = TYPE_HAPROT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(HAProtDevice),
    .instance_init = haprot_init,
    .class_init    = haprot_class_init,
    .class_size    = sizeof(HAProtDeviceClass),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void haprot_register_types(void)
{
    type_register_static(&haprot_info);
}

type_init(haprot_register_types)
