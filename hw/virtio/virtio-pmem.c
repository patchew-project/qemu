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

    /* flush raw backing image */
    err = fsync(req->fd);
    if (err != 0) {
        err = errno;
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
    virtio_stq_p(vdev, &pmemcfg->size, pmem->size);
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
    MachineState   *ms     = MACHINE(qdev_get_machine());
    uint64_t align;
    Error *local_err = NULL;
    MemoryRegion *mr;

    if (!pmem->memdev) {
        error_setg(errp, "virtio-pmem memdev not set");
        return;
    }

    mr  = host_memory_backend_get_memory(pmem->memdev);
    align = memory_region_get_alignment(mr);
    pmem->size = QEMU_ALIGN_DOWN(memory_region_size(mr), align);
    pmem->start = memory_device_get_free_addr(ms, NULL, align, pmem->size,
                                                               &local_err);
    if (local_err) {
        error_setg(errp, "Can't get free address in mem device");
        return;
    }
    memory_region_init_alias(&pmem->mr, OBJECT(pmem),
                             "virtio_pmem-memory", mr, 0, pmem->size);
    memory_device_plug_region(ms, &pmem->mr, pmem->start);

    host_memory_backend_set_mapped(pmem->memdev, true);
    virtio_init(vdev, TYPE_VIRTIO_PMEM, VIRTIO_ID_PMEM,
                                          sizeof(struct virtio_pmem_config));
    pmem->rq_vq = virtio_add_queue(vdev, 128, virtio_pmem_flush);
}

static void virtio_mem_check_memdev(Object *obj, const char *name, Object *val,
                                    Error **errp)
{
    if (host_memory_backend_is_mapped(MEMORY_BACKEND(val))) {
        char *path = object_get_canonical_path_component(val);
        error_setg(errp, "Can't use already busy memdev: %s", path);
        g_free(path);
        return;
    }

    qdev_prop_allow_set_link_before_realize(obj, name, val, errp);
}

static const char *virtio_pmem_get_device_id(VirtIOPMEM *vm)
{
    Object *obj = OBJECT(vm);
    DeviceState *parent_dev;

    /* always use the ID of the proxy device */
    if (obj->parent && object_dynamic_cast(obj->parent, TYPE_DEVICE)) {
        parent_dev = DEVICE(obj->parent);
        return parent_dev->id;
    }
    return NULL;
}

static void virtio_pmem_md_fill_device_info(const MemoryDeviceState *md,
                                           MemoryDeviceInfo *info)
{
    VirtioPMemDeviceInfo *vi = g_new0(VirtioPMemDeviceInfo, 1);
    VirtIOPMEM *vm = VIRTIO_PMEM(md);
    const char *id = virtio_pmem_get_device_id(vm);

    if (id) {
        vi->has_id = true;
        vi->id = g_strdup(id);
    }

    vi->start = vm->start;
    vi->size = vm->size;
    vi->memdev = object_get_canonical_path(OBJECT(vm->memdev));

    info->u.virtio_pmem.data = vi;
    info->type = MEMORY_DEVICE_INFO_KIND_VIRTIO_PMEM;
}

static uint64_t virtio_pmem_md_get_addr(const MemoryDeviceState *md)
{
    VirtIOPMEM *vm = VIRTIO_PMEM(md);

    return vm->start;
}

static uint64_t virtio_pmem_md_get_plugged_size(const MemoryDeviceState *md)
{
    VirtIOPMEM *vm = VIRTIO_PMEM(md);

    return vm->size;
}

static uint64_t virtio_pmem_md_get_region_size(const MemoryDeviceState *md)
{
    VirtIOPMEM *vm = VIRTIO_PMEM(md);

    return vm->size;
}

static void virtio_pmem_instance_init(Object *obj)
{
    VirtIOPMEM *vm = VIRTIO_PMEM(obj);
    object_property_add_link(obj, "memdev", TYPE_MEMORY_BACKEND,
                                (Object **)&vm->memdev,
                                (void *) virtio_mem_check_memdev,
                                OBJ_PROP_LINK_STRONG,
                                &error_abort);
}


static void virtio_pmem_class_init(ObjectClass *klass, void *data)
{
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);

    vdc->realize      =  virtio_pmem_realize;
    vdc->get_config   =  virtio_pmem_get_config;
    vdc->get_features =  virtio_pmem_get_features;

    mdc->get_addr         = virtio_pmem_md_get_addr;
    mdc->get_plugged_size = virtio_pmem_md_get_plugged_size;
    mdc->get_region_size  = virtio_pmem_md_get_region_size;
    mdc->fill_device_info = virtio_pmem_md_fill_device_info;
}

static TypeInfo virtio_pmem_info = {
    .name          = TYPE_VIRTIO_PMEM,
    .parent        = TYPE_VIRTIO_DEVICE,
    .class_init    = virtio_pmem_class_init,
    .instance_size = sizeof(VirtIOPMEM),
    .instance_init = virtio_pmem_instance_init,
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
