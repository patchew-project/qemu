/*
 * vhost-user-rdma.c
 *
 * Copyright (c) 2025 Weimin Xiong <xiongweimin@kylinos.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user-rdma.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

#define VHOST_USER_RDMA_NUM_QUEUES 256
#define VHOST_USER_RDMA_QUEUE_SIZE 512

static const int user_feature_bits[] = {
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VHOST_INVALID_FEATURE_BIT
};

static int vhost_user_rdma_start(VirtIODevice *vdev)
{
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&r->dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, r->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    r->dev.acked_features = vdev->guest_features;

    ret = vhost_dev_start(&r->dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }
    r->started_vu = true;

    for (i = 0; i < r->dev.nvqs; i++) {
        vhost_virtqueue_mask(&r->dev, vdev, i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, r->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&r->dev, vdev);
    return ret;
}

static void vhost_user_rdma_stop(VirtIODevice *vdev)
{
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!r->started_vu) {
        return;
    }
    r->started_vu = false;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&r->dev, vdev, true);

    ret = k->set_guest_notifiers(qbus->parent, r->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&r->dev, vdev);
}

static int vhost_user_rdma_handle_config_change(struct vhost_dev *dev)
{
    int ret;
    VhostUserRdma *r = VHOST_USER_RDMA(dev->vdev);
    Error *local_err = NULL;

    ret = vhost_dev_get_config(dev, (uint8_t *)&r->rdma_backend_cfg,
                               sizeof(struct virtio_rdma_config), &local_err);
    if (ret < 0) {
        error_report("get config space failed");
        return -1;
    }

    virtio_notify_config(dev->vdev);
    return 0;
}

const VhostDevConfigOps rdma_ops = {
    .vhost_dev_config_notifier = vhost_user_rdma_handle_config_change,
};

static int vhost_user_rdma_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);
    int ret = 0;
    Error *local_err = NULL;

    if (r->connected) {
        return 0;
    }
    r->connected = true;

    r->dev.nvqs = r->num_queues;
    r->dev.vqs = r->vhost_vqs;
    r->dev.vq_index = 0;
    r->dev.backend_features = 0;

    vhost_dev_set_config_notifier(&r->dev, &rdma_ops);

    ret = vhost_dev_init(&r->dev, &r->vu_state,
                         VHOST_BACKEND_TYPE_USER, 0, &local_err);
    if (ret < 0) {
        error_report("vhost-user-rdma: vhost initialization failed: %s",
                     strerror(-ret));
        return ret;
    }

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        info_report("vhost_user_rdma: vhost ss?");
        ret = vhost_user_rdma_start(vdev);
        if (ret < 0) {
            error_report("vhost-user-rdma: vhost start failed: %s",
                         strerror(-ret));
            return ret;
        }
    }

    return 0;
}

static void vhost_user_rdma_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostUserRdma *s = VHOST_USER_RDMA(vdev);

    if (!s->connected) {
        return;
    }
    s->connected = false;

    vhost_user_rdma_stop(vdev);

    vhost_dev_cleanup(&s->dev);
}

static void vhost_user_rdma_event(void *opaque, QEMUChrEvent event);

static void vhost_user_rdma_chr_closed_bh(void *opaque)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);

    vhost_user_rdma_disconnect(dev);
    qemu_chr_fe_set_handlers(&r->chardev, NULL, NULL, vhost_user_rdma_event,
            NULL, opaque, NULL, true);
}

static void vhost_user_rdma_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vhost_user_rdma_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&r->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        if (runstate_is_running()) {
            AioContext *ctx = qemu_get_current_aio_context();

            qemu_chr_fe_set_handlers(&r->chardev, NULL, NULL, NULL, NULL,
                    NULL, NULL, false);
            aio_bh_schedule_oneshot(ctx, vhost_user_rdma_chr_closed_bh, opaque);
        }

        r->dev.started = false;
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vhost_user_rdma_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (!r->connected) {
        return;
    }

    if (r->dev.started) {
        return;
    }

    ret = vhost_user_rdma_start(vdev);
    if (ret < 0) {
        error_report("vhost-user-rdma: vhost start failed: %s",
                     strerror(-ret));
        qemu_chr_fe_disconnect(&r->chardev);
        return;
    }

    for (i = 0; i < r->dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static void vhost_user_rdma_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);

    memcpy(config, &r->rdma_backend_cfg, sizeof(struct virtio_rdma_config));
}

static uint64_t vhost_user_rdma_get_features(VirtIODevice *vdev,
                                            uint64_t features,
                                            Error **errp)
{
    VhostUserRdma *s = VHOST_USER_RDMA(vdev);

    return vhost_get_features(&s->dev, user_feature_bits, features);
}

static void vhost_user_rdma_set_status(VirtIODevice *vdev, uint8_t status)
{
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);
    bool should_start = virtio_device_started(vdev, status);
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (!r->connected) {
        return;
    }

    if (r->dev.started == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_user_rdma_start(vdev);
        if (ret < 0) {
            error_report("vhost-user-rdma: vhost start failed: %s",
                         strerror(-ret));
            qemu_chr_fe_disconnect(&r->chardev);
        }
    } else {
        vhost_user_rdma_stop(vdev);
    }
}

static void vhost_user_rdma_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostUserRdma *r = VHOST_USER_RDMA(vdev);
    Error *err = NULL;
    int i, ret;

    if (!r->chardev.chr) {
        error_setg(errp, "vhost-user-rdma: chardev is mandatory");
        return;
    }

    r->num_queues = VHOST_USER_RDMA_NUM_QUEUES;

    if (r->num_queues > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "vhost-user-rdma: invalid number of IO queues");
        return;
    }

    if (!vhost_user_init(&r->vu_state, &r->chardev, errp)) {
        return;
    }

    virtio_init(vdev, VIRTIO_ID_RDMA, sizeof(struct virtio_rdma_config));

    r->virtqs = g_new(VirtQueue *, r->num_queues);

    for (i = 0; i < r->num_queues; i++) {
        r->virtqs[i] = virtio_add_queue(vdev, VHOST_USER_RDMA_QUEUE_SIZE,
                                        vhost_user_rdma_handle_output);
    }

    r->vhost_vqs = g_new0(struct vhost_virtqueue, r->num_queues);
    r->connected = false;

    qemu_chr_fe_set_handlers(&r->chardev,  NULL, NULL, vhost_user_rdma_event,
                             NULL, (void *)dev, NULL, true);

reconnect:
    if (qemu_chr_fe_wait_connected(&r->chardev, &err) < 0) {
        error_report_err(err);
        goto virtio_err;
    }

    /* check whether vhost_user_rdma_connect() failed or not */
    if (!r->connected) {
        goto reconnect;
    }

    ret = vhost_dev_get_config(&r->dev, (uint8_t *)&r->rdma_backend_cfg,
                               sizeof(struct virtio_rdma_config), &err);
    if (ret < 0) {
        error_report("vhost-user-rdma: get rdma config failed");
        goto reconnect;
    }

    return;

virtio_err:
    g_free(r->vhost_vqs);
    r->vhost_vqs = NULL;
    for (i = 0; i < r->num_queues; i++) {
        virtio_delete_queue(r->virtqs[i]);
    }
    g_free(r->virtqs);
    virtio_cleanup(vdev);
    vhost_user_cleanup(&r->vu_state);
}

static void vhost_user_rdma_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostUserRdma *r = VHOST_USER_RDMA(dev);
    int i;

    virtio_set_status(vdev, 0);
    qemu_chr_fe_set_handlers(&r->chardev,  NULL, NULL, NULL,
                             NULL, NULL, NULL, false);
    vhost_dev_cleanup(&r->dev);
    g_free(r->vhost_vqs);
    r->vhost_vqs = NULL;
    for (i = 0; i < r->num_queues; i++) {
        virtio_delete_queue(r->virtqs[i]);
    }
    g_free(r->virtqs);
    virtio_cleanup(vdev);
    vhost_user_cleanup(&r->vu_state);
}

static void vhost_user_rdma_instance_init(Object *obj)
{
    VhostUserRdma *r = VHOST_USER_RDMA(obj);

    device_add_bootindex_property(obj, &r->bootindex, "bootindex",
                                  "bootindex", DEVICE(obj));
}

static const VMStateDescription vmstate_vhost_user_rdma = {
    .name = "vhost-user-rdma",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_user_rdma_properties[] = {
    DEFINE_PROP_CHR("chardev", VhostUserRdma, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_rdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_user_rdma_properties);
    dc->vmsd = &vmstate_vhost_user_rdma;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);

    vdc->realize = vhost_user_rdma_device_realize;
    vdc->unrealize = vhost_user_rdma_device_unrealize;
    vdc->get_config = vhost_user_rdma_update_config;
    vdc->set_config = NULL;
    vdc->get_features = vhost_user_rdma_get_features;
    vdc->set_status = vhost_user_rdma_set_status;
}

static const TypeInfo vhost_user_rdma_info = {
    .name = TYPE_VHOST_USER_RDMA,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VhostUserRdma),
    .instance_init = vhost_user_rdma_instance_init,
    .class_init = vhost_user_rdma_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_user_rdma_info);
}

type_init(virtio_register_types)
