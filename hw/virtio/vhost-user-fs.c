/*
 * Vhost-user filesystem virtio device
 *
 * Copyright 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include "standard-headers/linux/virtio_fs.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/error-report.h"
#include "hw/virtio/vhost-user-fs.h"
#include "monitor/monitor.h"
#include "sysemu/runstate.h"

static void vuf_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    struct virtio_fs_config fscfg = {};

    memcpy((char *)fscfg.tag, fs->conf.tag,
           MIN(strlen(fs->conf.tag) + 1, sizeof(fscfg.tag)));

    virtio_stl_p(vdev, &fscfg.num_request_queues, fs->conf.num_request_queues);

    memcpy(config, &fscfg, sizeof(fscfg));
}

static int vuf_start(VirtIODevice *vdev)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;
    int i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&fs->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, fs->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    fs->vhost_dev.acked_features = vdev->guest_features;

    if (!fs->inflight->addr) {
        ret = vhost_dev_get_inflight(&fs->vhost_dev, fs->conf.queue_size,
                                     fs->inflight);
        if (ret < 0) {
            error_report("Error get inflight: %d", -ret);
            goto err_guest_notifiers;
        }
    }

    ret = vhost_dev_set_shm(&fs->vhost_dev);
    if (ret < 0) {
        error_report("Error set fs maps: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_dev_set_fd(&fs->vhost_dev);
    if (ret < 0) {
        error_report("Error set fs proc fds: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_dev_set_inflight(&fs->vhost_dev, fs->inflight);
    if (ret < 0) {
        error_report("Error set inflight: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_dev_start(&fs->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < fs->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&fs->vhost_dev, vdev, i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, fs->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&fs->vhost_dev, vdev);

    return ret;
}

static void vuf_stop(VirtIODevice *vdev)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&fs->vhost_dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, fs->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&fs->vhost_dev, vdev);
}

static void vuf_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (!fs->connected) {
        return;
    }

    if (fs->vhost_dev.started == should_start) {
        return;
    }

    if (should_start) {
        ret = vuf_start(vdev);
        if (ret < 0) {
            error_report("vhost-user-fs: vhost start failed: %s",
                         strerror(-ret));
            qemu_chr_fe_disconnect(&fs->conf.chardev);
        }
    } else {
        vuf_stop(vdev);
    }
}

static uint64_t vuf_get_features(VirtIODevice *vdev,
                                      uint64_t requested_features,
                                      Error **errp)
{
    /* No feature bits used yet */
    return requested_features;
}

static void vuf_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (!fs->connected) {
        return;
    }

    if (fs->vhost_dev.started) {
        return;
    }

    /*
     * Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
     * vhost here instead of waiting for .set_status().
     */
    ret = vuf_start(vdev);
    if (ret < 0) {
        error_report("vhost-user-fs: vhost start failed: %s",
                     strerror(-ret));
        qemu_chr_fe_disconnect(&fs->conf.chardev);
        return;
    }

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < fs->vhost_dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static bool vuf_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    return vhost_virtqueue_pending(&fs->vhost_dev, idx);
}

static void vuf_reset(VirtIODevice *vdev)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    vhost_dev_free_inflight(fs->inflight);
}

static int vhost_user_fs_persist_map(struct vhost_dev *dev,
                                     struct VhostUserShm *shm, int fd)
{
    VHostUserFS *fs = container_of(dev, VHostUserFS, vhost_dev);
    VhostUserFSPersist *persist = &fs->persist;

    if (persist->map_fds[shm->id] != -1) {
        close(persist->map_fds[shm->id]);
    }

    persist->need_restore = true;
    memcpy(&persist->maps[shm->id], shm, sizeof(VhostUserShm));
    persist->map_fds[shm->id] = dup(fd);

    return 0;
}

static int vhost_user_fs_map_info(struct vhost_dev *dev, int id,
                                  uint64_t *size, uint64_t *offset,
                                  int *memfd)
{
    if (!dev) {
        return -1;
    }

    if (id >= MAP_TYPE_NUM) {
        return -1;
    }

    VHostUserFS *fs = container_of(dev, VHostUserFS, vhost_dev);
    VhostUserFSPersist *persist = &fs->persist;
    if (!persist->need_restore || (persist->map_fds[id] == -1)) {
        return -1;
    }

    *size = persist->maps[id].size;
    *offset = persist->maps[id].offset;
    *memfd = persist->map_fds[id];

    return 0;
}

static int vhost_user_fs_persist_fd(struct vhost_dev *dev,
                                    struct VhostUserFd *fdinfo, int fd)
{
    VHostUserFS *fs = container_of(dev, VHostUserFS, vhost_dev);
    VhostUserFSPersist *persist = &fs->persist;

    persist->need_restore = true;

    if (fdinfo->flag == VU_FD_FLAG_ADD) {
        assert(persist->fd_ht != NULL);
        int newfd = dup(fd);
        g_hash_table_insert(persist->fd_ht, GINT_TO_POINTER(fdinfo->key),
                                                    GINT_TO_POINTER(newfd));
    } else if (fdinfo->flag == VU_FD_FLAG_DEL) {
        gpointer fd_p = g_hash_table_lookup(persist->fd_ht,
                                            GINT_TO_POINTER(fdinfo->key));
        if (fd_p != NULL) {
            int fd = GPOINTER_TO_INT(fd_p);
            close(fd);
            g_hash_table_remove(persist->fd_ht,
                                        GINT_TO_POINTER(fdinfo->key));
        }
    }

    return 0;
}

static int vhost_user_fs_fd_info(struct vhost_dev *dev, GHashTable **fd_ht_p)
{
    if (!dev) {
        return -1;
    }

    VHostUserFS *fs = container_of(dev, VHostUserFS, vhost_dev);
    VhostUserFSPersist *persist = &fs->persist;
    if (!persist->need_restore) {
        return -1;
    }

    *fd_ht_p = persist->fd_ht;
    return 0;
}


const VhostDevShmOps fs_shm_ops = {
        .vhost_dev_slave_shm = vhost_user_fs_persist_map,
        .vhost_dev_shm_info = vhost_user_fs_map_info,
};

const VhostDevFdOps fs_fd_ops = {
        .vhost_dev_slave_fd = vhost_user_fs_persist_fd,
        .vhost_dev_fd_info = vhost_user_fs_fd_info,
};

static int vuf_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    int ret = 0;

    if (fs->connected) {
        return 0;
    }
    fs->connected = true;

    /* 1 high prio queue, plus the number configured */
    fs->vhost_dev.nvqs = 1 + fs->conf.num_request_queues;
    fs->vhost_dev.vqs = g_new0(struct vhost_virtqueue, fs->vhost_dev.nvqs);
    ret = vhost_dev_init(&fs->vhost_dev, &fs->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_report("vhost-user-fs: vhost initialization failed: %s",
                     strerror(-ret));
        return ret;
    }

    vhost_dev_set_shm_ops(&fs->vhost_dev, &fs_shm_ops);
    vhost_dev_set_fd_ops(&fs->vhost_dev, &fs_fd_ops);

    /* restore vhost state */
    if (vdev->started) {
        ret = vuf_start(vdev);
        if (ret < 0) {
            error_report("vhost-user-fs: vhost start failed: %s",
                         strerror(-ret));
            return ret;
        }
    }

    return 0;
}

static void vuf_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    if (!fs->connected) {
        return;
    }
    fs->connected = false;

    if (fs->vhost_dev.started) {
        vuf_stop(vdev);
    }

    vhost_dev_cleanup(&fs->vhost_dev);
}

static void vuf_event(void *opaque, QEMUChrEvent event);

static void vuf_chr_closed_bh(void *opaque)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    vuf_disconnect(dev);
    qemu_chr_fe_set_handlers(&fs->conf.chardev, NULL, NULL, vuf_event,
            NULL, opaque, NULL, true);
}

static void vuf_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vuf_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&fs->conf.chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        /* delay disconnectting according to commit 4bcad76f4c390f */
        if (runstate_is_running()) {
            AioContext *ctx = qemu_get_current_aio_context();

            qemu_chr_fe_set_handlers(&fs->conf.chardev, NULL, NULL, NULL, NULL,
                    NULL, NULL, false);
            aio_bh_schedule_oneshot(ctx, vuf_chr_closed_bh, opaque);
        }
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
         /* Ignore */
            break;
    }
}


static void vuf_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(dev);
    unsigned int i;
    size_t len;
    Error *err = NULL;

    if (!fs->conf.chardev.chr) {
        error_setg(errp, "missing chardev");
        return;
    }

    if (!fs->conf.tag) {
        error_setg(errp, "missing tag property");
        return;
    }
    len = strlen(fs->conf.tag);
    if (len == 0) {
        error_setg(errp, "tag property cannot be empty");
        return;
    }
    if (len > sizeof_field(struct virtio_fs_config, tag)) {
        error_setg(errp, "tag property must be %zu bytes or less",
                   sizeof_field(struct virtio_fs_config, tag));
        return;
    }

    if (fs->conf.num_request_queues == 0) {
        error_setg(errp, "num-request-queues property must be larger than 0");
        return;
    }

    if (!is_power_of_2(fs->conf.queue_size)) {
        error_setg(errp, "queue-size property must be a power of 2");
        return;
    }

    if (fs->conf.queue_size > VIRTQUEUE_MAX_SIZE) {
        error_setg(errp, "queue-size property must be %u or smaller",
                   VIRTQUEUE_MAX_SIZE);
        return;
    }

    if (!vhost_user_init(&fs->vhost_user, &fs->conf.chardev, errp)) {
        return;
    }

    virtio_init(vdev, "vhost-user-fs", VIRTIO_ID_FS,
                sizeof(struct virtio_fs_config));

    /* Hiprio queue */
    fs->hiprio_vq = virtio_add_queue(vdev, fs->conf.queue_size, vuf_handle_output);

    /* Request queues */
    fs->req_vqs = g_new(VirtQueue *, fs->conf.num_request_queues);
    for (i = 0; i < fs->conf.num_request_queues; i++) {
        fs->req_vqs[i] = virtio_add_queue(vdev, fs->conf.queue_size, vuf_handle_output);
    }

    /* init reconnection related variables */
    fs->inflight = g_new0(struct vhost_inflight, 1);
    fs->connected = false;
    fs->persist.need_restore = false;
    for (i = 0; i < MAP_TYPE_NUM; i++) {
        fs->persist.map_fds[i] = -1;
    }
    fs->persist.fd_ht = g_hash_table_new(NULL, NULL);
    qemu_chr_fe_set_handlers(&fs->conf.chardev,  NULL, NULL, vuf_event,
                                 NULL, (void *)dev, NULL, true);

reconnect:
    if (qemu_chr_fe_wait_connected(&fs->conf.chardev, &err) < 0) {
        error_report_err(err);
        goto err_virtio;
    }

    /* check whether vuf_connect() failed or not */
    if (!fs->connected) {
        goto reconnect;
    }

    return;

err_virtio:
    vhost_user_cleanup(&fs->vhost_user);
    virtio_delete_queue(fs->hiprio_vq);
    for (i = 0; i < fs->conf.num_request_queues; i++) {
        virtio_delete_queue(fs->req_vqs[i]);
    }
    g_free(fs->req_vqs);
    fs->req_vqs = NULL;
    g_free(fs->inflight);
    fs->inflight = NULL;
    virtio_cleanup(vdev);
    g_free(fs->vhost_dev.vqs);
    return;
}

static void vuf_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(dev);
    int i;

    /* This will stop vhost backend if appropriate. */
    virtio_set_status(vdev, 0);

    vhost_dev_cleanup(&fs->vhost_dev);

    vhost_user_cleanup(&fs->vhost_user);

    virtio_delete_queue(fs->hiprio_vq);
    for (i = 0; i < fs->conf.num_request_queues; i++) {
        virtio_delete_queue(fs->req_vqs[i]);
    }
    g_free(fs->req_vqs);
    fs->req_vqs = NULL;
    qemu_chr_fe_set_handlers(&fs->conf.chardev,  NULL, NULL, NULL,
                             NULL, NULL, NULL, false);

    virtio_cleanup(vdev);
    vhost_dev_free_inflight(fs->inflight);
    g_free(fs->vhost_dev.vqs);
    fs->vhost_dev.vqs = NULL;
    g_free(fs->inflight);
    fs->inflight = NULL;
    g_hash_table_destroy(fs->persist.fd_ht);
}

static const VMStateDescription vuf_vmstate = {
    .name = "vhost-user-fs",
    .unmigratable = 1,
};

static Property vuf_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserFS, conf.chardev),
    DEFINE_PROP_STRING("tag", VHostUserFS, conf.tag),
    DEFINE_PROP_UINT16("num-request-queues", VHostUserFS,
                       conf.num_request_queues, 1),
    DEFINE_PROP_UINT16("queue-size", VHostUserFS, conf.queue_size, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static void vuf_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vuf_properties);
    dc->vmsd = &vuf_vmstate;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vuf_device_realize;
    vdc->unrealize = vuf_device_unrealize;
    vdc->get_features = vuf_get_features;
    vdc->get_config = vuf_get_config;
    vdc->set_status = vuf_set_status;
    vdc->guest_notifier_pending = vuf_guest_notifier_pending;
    vdc->reset = vuf_reset;
}

static const TypeInfo vuf_info = {
    .name = TYPE_VHOST_USER_FS,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserFS),
    .class_init = vuf_class_init,
};

static void vuf_register_types(void)
{
    type_register_static(&vuf_info);
}

type_init(vuf_register_types)
