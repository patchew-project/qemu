/*
 * Virtio QMP helpers
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "virtio-qmp.h"

#include "qapi/error.h"
#include "qapi/qapi-commands-virtio.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qjson.h"

static int query_dev_child(Object *child, void *opaque)
{
    VirtioInfoList **vdevs = opaque;
    Object *dev = object_dynamic_cast(child, TYPE_VIRTIO_DEVICE);
    if (dev != NULL && DEVICE(dev)->realized) {
        VirtIODevice *vdev = VIRTIO_DEVICE(dev);
        VirtioInfo *info = g_new(VirtioInfo, 1);

        /* Get canonical path & name of device */
        info->path = object_get_canonical_path(dev);
        info->name = g_strdup(vdev->name);
        QAPI_LIST_PREPEND(*vdevs, info);
    }
    return 0;
}

VirtioInfoList *qmp_x_query_virtio(Error **errp)
{
    VirtioInfoList *vdevs = NULL;

    /* Query the QOM composition tree recursively for virtio devices */
    object_child_foreach_recursive(object_get_root(), query_dev_child, &vdevs);
    if (vdevs == NULL) {
        error_setg(errp, "No virtio devices found");
    }
    return vdevs;
}

VirtIODevice *qmp_find_virtio_device(const char *path)
{
    /* Verify the canonical path is a realized virtio device */
    Object *dev = object_dynamic_cast(object_resolve_path(path, NULL),
                                      TYPE_VIRTIO_DEVICE);
    if (!dev || !DEVICE(dev)->realized) {
        return NULL;
    }
    return VIRTIO_DEVICE(dev);
}

VirtioStatus *qmp_x_query_virtio_status(const char *path, Error **errp)
{
    VirtIODevice *vdev;
    VirtioStatus *status;

    vdev = qmp_find_virtio_device(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a realized VirtIODevice", path);
        return NULL;
    }

    status = g_new0(VirtioStatus, 1);
    status->name = g_strdup(vdev->name);
    status->device_id = vdev->device_id;
    status->vhost_started = vdev->vhost_started;
    status->guest_features = vdev->guest_features;
    status->host_features = vdev->host_features;
    status->backend_features = vdev->backend_features;

    switch (vdev->device_endian) {
    case VIRTIO_DEVICE_ENDIAN_LITTLE:
        status->device_endian = g_strdup("little");
        break;
    case VIRTIO_DEVICE_ENDIAN_BIG:
        status->device_endian = g_strdup("big");
        break;
    default:
        status->device_endian = g_strdup("unknown");
        break;
    }

    status->num_vqs = virtio_get_num_queues(vdev);
    status->status = vdev->status;
    status->isr = vdev->isr;
    status->queue_sel = vdev->queue_sel;
    status->vm_running = vdev->vm_running;
    status->broken = vdev->broken;
    status->disabled = vdev->disabled;
    status->use_started = vdev->use_started;
    status->started = vdev->started;
    status->start_on_kick = vdev->start_on_kick;
    status->disable_legacy_check = vdev->disable_legacy_check;
    status->bus_name = g_strdup(vdev->bus_name);
    status->use_guest_notifier_mask = vdev->use_guest_notifier_mask;

    if (vdev->vhost_started) {
        VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
        struct vhost_dev *hdev = vdc->get_vhost(vdev);

        status->vhost_dev = g_new0(VhostStatus, 1);
        status->vhost_dev->n_mem_sections = hdev->n_mem_sections;
        status->vhost_dev->n_tmp_sections = hdev->n_tmp_sections;
        status->vhost_dev->nvqs = hdev->nvqs;
        status->vhost_dev->vq_index = hdev->vq_index;
        status->vhost_dev->features = hdev->features;
        status->vhost_dev->acked_features = hdev->acked_features;
        status->vhost_dev->backend_features = hdev->backend_features;
        status->vhost_dev->protocol_features = hdev->protocol_features;
        status->vhost_dev->max_queues = hdev->max_queues;
        status->vhost_dev->backend_cap = hdev->backend_cap;
        status->vhost_dev->log_enabled = hdev->log_enabled;
        status->vhost_dev->log_size = hdev->log_size;
    }

    return status;
}

VirtVhostQueueStatus *qmp_x_query_virtio_vhost_queue_status(const char *path,
                                                            uint16_t queue,
                                                            Error **errp)
{
    VirtIODevice *vdev;
    VirtVhostQueueStatus *status;

    vdev = qmp_find_virtio_device(path);
    if (vdev == NULL) {
        error_setg(errp, "Path %s is not a VirtIODevice", path);
        return NULL;
    }

    if (!vdev->vhost_started) {
        error_setg(errp, "Error: vhost device has not started yet");
        return NULL;
    }

    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    struct vhost_dev *hdev = vdc->get_vhost(vdev);

    if (queue < hdev->vq_index || queue >= hdev->vq_index + hdev->nvqs) {
        error_setg(errp, "Invalid vhost virtqueue number %d", queue);
        return NULL;
    }

    status = g_new0(VirtVhostQueueStatus, 1);
    status->name = g_strdup(vdev->name);
    status->kick = hdev->vqs[queue].kick;
    status->call = hdev->vqs[queue].call;
    status->desc = (uintptr_t)hdev->vqs[queue].desc;
    status->avail = (uintptr_t)hdev->vqs[queue].avail;
    status->used = (uintptr_t)hdev->vqs[queue].used;
    status->num = hdev->vqs[queue].num;
    status->desc_phys = hdev->vqs[queue].desc_phys;
    status->desc_size = hdev->vqs[queue].desc_size;
    status->avail_phys = hdev->vqs[queue].avail_phys;
    status->avail_size = hdev->vqs[queue].avail_size;
    status->used_phys = hdev->vqs[queue].used_phys;
    status->used_size = hdev->vqs[queue].used_size;

    return status;
}
