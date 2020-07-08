/*
 * Virtio MEM CCW device
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-ccw.h"
#include "hw/mem/memory-device.h"
#include "qapi/qapi-events-misc.h"

static void virtio_ccw_mem_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOMEMCcw *ccw_mem = VIRTIO_MEM_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&ccw_mem->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_mem_set_addr(MemoryDeviceState *md, uint64_t addr,
                                    Error **errp)
{
    object_property_set_uint(OBJECT(md), addr, VIRTIO_MEM_ADDR_PROP, errp);
}

static uint64_t virtio_ccw_mem_get_addr(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_ADDR_PROP,
                                    &error_abort);
}

static MemoryRegion *virtio_ccw_mem_get_memory_region(MemoryDeviceState *md,
                                                      Error **errp)
{
    VirtIOMEMCcw *ccw_mem = VIRTIO_MEM_CCW(md);
    VirtIOMEM *vmem = VIRTIO_MEM(&ccw_mem->vdev);
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    return vmc->get_memory_region(vmem, errp);
}

static uint64_t virtio_ccw_mem_get_plugged_size(const MemoryDeviceState *md,
                                                Error **errp)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_SIZE_PROP,
                                    errp);
}

static void virtio_ccw_mem_fill_device_info(const MemoryDeviceState *md,
                                            MemoryDeviceInfo *info)
{
    VirtioMEMDeviceInfo *vi = g_new0(VirtioMEMDeviceInfo, 1);
    VirtIOMEMCcw *ccw_mem = VIRTIO_MEM_CCW(md);
    VirtIOMEM *vmem = VIRTIO_MEM(&ccw_mem->vdev);
    VirtIOMEMClass *vpc = VIRTIO_MEM_GET_CLASS(vmem);
    DeviceState *dev = DEVICE(md);

    if (dev->id) {
        vi->has_id = true;
        vi->id = g_strdup(dev->id);
    }

    /* let the real device handle everything else */
    vpc->fill_device_info(vmem, vi);

    info->u.virtio_mem.data = vi;
    info->type = MEMORY_DEVICE_INFO_KIND_VIRTIO_MEM;
}

static void virtio_ccw_mem_size_change_notify(Notifier *notifier, void *data)
{
    VirtIOMEMCcw *ccw_mem = container_of(notifier, VirtIOMEMCcw,
                                         size_change_notifier);
    DeviceState *dev = DEVICE(ccw_mem);
    const uint64_t * const size_p = data;
    const char *id = NULL;

    if (dev->id) {
        id = g_strdup(dev->id);
    }

    qapi_event_send_memory_device_size_change(!!id, id, *size_p);
}

static void virtio_ccw_mem_instance_init(Object *obj)
{
    VirtIOMEMCcw *ccw_mem = VIRTIO_MEM_CCW(obj);
    VirtIOMEMClass *vmc;
    VirtIOMEM *vmem;

    virtio_instance_init_common(obj, &ccw_mem->vdev, sizeof(ccw_mem->vdev),
                                TYPE_VIRTIO_MEM);

    ccw_mem->size_change_notifier.notify = virtio_ccw_mem_size_change_notify;
    vmem = VIRTIO_MEM(&ccw_mem->vdev);
    vmc = VIRTIO_MEM_GET_CLASS(vmem);
    /*
     * We never remove the notifier again, as we expect both devices to
     * disappear at the same time.
     */
    vmc->add_size_change_notifier(vmem, &ccw_mem->size_change_notifier);

    object_property_add_alias(obj, VIRTIO_MEM_BLOCK_SIZE_PROP,
                              OBJECT(&ccw_mem->vdev),
                              VIRTIO_MEM_BLOCK_SIZE_PROP);
    object_property_add_alias(obj, VIRTIO_MEM_SIZE_PROP, OBJECT(&ccw_mem->vdev),
                              VIRTIO_MEM_SIZE_PROP);
    object_property_add_alias(obj, VIRTIO_MEM_REQUESTED_SIZE_PROP,
                              OBJECT(&ccw_mem->vdev),
                              VIRTIO_MEM_REQUESTED_SIZE_PROP);
}

static Property virtio_ccw_mem_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_ccw_mem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_mem_realize;
    device_class_set_props(dc, virtio_ccw_mem_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    mdc->get_addr = virtio_ccw_mem_get_addr;
    mdc->set_addr = virtio_ccw_mem_set_addr;
    mdc->get_plugged_size = virtio_ccw_mem_get_plugged_size;
    mdc->get_memory_region = virtio_ccw_mem_get_memory_region;
    mdc->fill_device_info = virtio_ccw_mem_fill_device_info;
}

static const TypeInfo virtio_ccw_mem = {
    .name = TYPE_VIRTIO_MEM_CCW,
    .parent = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIOMEMCcw),
    .instance_init = virtio_ccw_mem_instance_init,
    .class_init = virtio_ccw_mem_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
    },
};

static void virtio_ccw_mem_register(void)
{
    type_register_static(&virtio_ccw_mem);
}

type_init(virtio_ccw_mem_register)
