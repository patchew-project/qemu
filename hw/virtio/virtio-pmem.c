/*
 * Virtio pmem device
 *
 * Copyright (C) 2018 Red Hat, Inc.
 * Copyright (C) 2018 Pankaj Gupta <pagupta@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-pmem.h"
#include "hw/mem/memory-device.h"
#include "block/aio.h"
#include "block/thread-pool.h"

typedef struct VirtIOPMEMresp {
    int ret;
} VirtIOPMEMResp;

typedef struct VirtIODeviceRequest {
    VirtQueueElement elem;
    int fd;
    VirtIOPMEM *pmem;
    VirtIOPMEMResp resp;
} VirtIODeviceRequest;

static int worker_cb(void *opaque)
{
    VirtIODeviceRequest *req = opaque;
    int err = 0;

    printf("\n performing flush ...");
    /* flush raw backing image */
    err = fsync(req->fd);
    printf("\n performed flush ...:errcode::%d", err);
    if (err != 0) {
        err = EIO;
    }
    req->resp.ret = err;

    return 0;
}

static void done_cb(void *opaque, int ret)
{
    VirtIODeviceRequest *req = opaque;
    int len = iov_from_buf(req->elem.in_sg, req->elem.in_num, 0,
                              &req->resp, sizeof(VirtIOPMEMResp));

    /* Callbacks are serialized, so no need to use atomic ops.  */
    virtqueue_push(req->pmem->rq_vq, &req->elem, len);
    virtio_notify((VirtIODevice *)req->pmem, req->pmem->rq_vq);
    g_free(req);
}

static void virtio_pmem_flush(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIODeviceRequest *req;
    VirtIOPMEM *pmem = VIRTIO_PMEM(vdev);
    HostMemoryBackend *backend = MEMORY_BACKEND(pmem->memdev);
    ThreadPool *pool = aio_get_thread_pool(qemu_get_aio_context());

    req = virtqueue_pop(vq, sizeof(VirtIODeviceRequest));
    if (!req) {
        virtio_error(vdev, "virtio-pmem missing request data");
        return;
    }

    if (req->elem.out_num < 1 || req->elem.in_num < 1) {
        virtio_error(vdev, "virtio-pmem request not proper");
        g_free(req);
        return;
    }
    req->fd = memory_region_get_fd(&backend->mr);
    req->pmem = pmem;
    thread_pool_submit_aio(pool, worker_cb, req, done_cb, req);
}

static void virtio_pmem_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOPMEM *pmem = VIRTIO_PMEM(vdev);
    struct virtio_pmem_config *pmemcfg = (struct virtio_pmem_config *) config;

    virtio_stq_p(vdev, &pmemcfg->start, pmem->start);
    virtio_stq_p(vdev, &pmemcfg->size, memory_region_size(&pmem->memdev->mr));
}

static uint64_t virtio_pmem_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    return features;
}

static void virtio_pmem_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice   *vdev   = VIRTIO_DEVICE(dev);
    VirtIOPMEM     *pmem   = VIRTIO_PMEM(dev);

    if (!pmem->memdev) {
        error_setg(errp, "virtio-pmem memdev not set");
        return;
    } else if (host_memory_backend_is_mapped(pmem->memdev)) {
        char *path = object_get_canonical_path_component(OBJECT(pmem->memdev));
        error_setg(errp, "can't use already busy memdev: %s", path);
        g_free(path);
        return;
    }

    /* pre_plug handler wasn't executed (e.g. from machine hotplug handler) */
    if (!pmem->pre_plugged) {
        error_setg(errp, "virtio-pmem is not compatible with the machine");
        return;
    }

    host_memory_backend_set_mapped(pmem->memdev, true);
    virtio_init(vdev, TYPE_VIRTIO_PMEM, VIRTIO_ID_PMEM,
                                          sizeof(struct virtio_pmem_config));
    pmem->rq_vq = virtio_add_queue(vdev, 128, virtio_pmem_flush);
}

static void virtio_pmem_md_fill_device_info(const MemoryDeviceState *md,
                                            MemoryDeviceInfo *info)
{
    VirtioPMemDeviceInfo *vi = g_new0(VirtioPMemDeviceInfo, 1);
    VirtIOPMEM *pmem = VIRTIO_PMEM(md);
    const char *id = memory_device_id(md);

    if (id) {
        vi->has_id = true;
        vi->id = g_strdup(id);
    }

    vi->memaddr = pmem->start;
    vi->size = pmem->memdev ? memory_region_size(&pmem->memdev->mr) : 0;
    vi->memdev = object_get_canonical_path(OBJECT(pmem->memdev));

    info->u.virtio_pmem.data = vi;
    info->type = MEMORY_DEVICE_INFO_KIND_VIRTIO_PMEM;
}

static uint64_t virtio_pmem_md_get_addr(const MemoryDeviceState *md)
{
    VirtIOPMEM *pmem = VIRTIO_PMEM(md);

    return pmem->start;
}

static void virtio_pmem_md_set_addr(MemoryDeviceState *md, uint64_t addr,
                                    Error **errp)
{
    object_property_set_uint(OBJECT(md), addr, VIRTIO_PMEM_ADDR_PROP, errp);
}

static uint64_t virtio_pmem_md_get_plugged_size(const MemoryDeviceState *md,
                                                Error **errp)
{
    VirtIOPMEM *pmem = VIRTIO_PMEM(md);

    if (!pmem->memdev) {
        error_setg(errp, "'%s' property must be set", VIRTIO_PMEM_MEMDEV_PROP);
        return 0;
    }

    return memory_region_size(&pmem->memdev->mr);
}

static MemoryRegion *virtio_pmem_md_get_memory_region(MemoryDeviceState *md,
                                                      Error **errp)
{
    VirtIOPMEM *pmem = VIRTIO_PMEM(md);

    if (!pmem->memdev) {
        error_setg(errp, "'%s' property must be set", VIRTIO_PMEM_MEMDEV_PROP);
        return NULL;
    }

    return &pmem->memdev->mr;
}

static Property virtio_pmem_properties[] = {
    DEFINE_PROP_UINT64(VIRTIO_PMEM_ADDR_PROP, VirtIOPMEM, start, 0),
    DEFINE_PROP_LINK(VIRTIO_PMEM_MEMDEV_PROP, VirtIOPMEM, memdev,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_pmem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);

    dc->props = virtio_pmem_properties;

    vdc->realize      =  virtio_pmem_realize;
    vdc->get_config   =  virtio_pmem_get_config;
    vdc->get_features =  virtio_pmem_get_features;

    mdc->get_addr         = virtio_pmem_md_get_addr;
    mdc->set_addr         = virtio_pmem_md_set_addr;
    mdc->get_plugged_size = virtio_pmem_md_get_plugged_size;
    mdc->get_memory_region  = virtio_pmem_md_get_memory_region;
    mdc->fill_device_info = virtio_pmem_md_fill_device_info;
}

void virtio_pmem_pre_plug(VirtIOPMEM *pmem, MachineState *ms, Error **errp)
{
    /*
     * The proxy device (e.g. virtio-pmem-pci) has an hotplug handler and
     * will attach the virtio-pmem device to its bus (parent_bus). This
     * device will realize the virtio-mem device from its realize function,
     * therefore when it is hotplugged itself. The proxy device bus
     * therefore has no hotplug handler and we don't have to forward any
     * calls.
     */
    if (!DEVICE(pmem)->parent_bus ||
        DEVICE(pmem)->parent_bus->hotplug_handler) {
        error_setg(errp, "virtio-pmem is not compatible with the proxy.");
    }
    memory_device_pre_plug(MEMORY_DEVICE(pmem), ms, NULL, errp);
    pmem->pre_plugged = true;
}

void virtio_pmem_plug(VirtIOPMEM *pmem, MachineState *ms, Error **errp)
{
    memory_device_plug(MEMORY_DEVICE(pmem), ms);
}

void virtio_pmem_unplug(VirtIOPMEM *pmem, MachineState *ms, Error **errp)
{
    memory_device_unplug(MEMORY_DEVICE(pmem), ms);
}

static TypeInfo virtio_pmem_info = {
    .name          = TYPE_VIRTIO_PMEM,
    .parent        = TYPE_VIRTIO_DEVICE,
    .class_init    = virtio_pmem_class_init,
    .instance_size = sizeof(VirtIOPMEM),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_MEMORY_DEVICE },
        { }
  },
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_pmem_info);
}

type_init(virtio_register_types)
