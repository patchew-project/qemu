/*
 * Virtio MSG bindings
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-msg-bus.h"
#include "hw/virtio/virtio-msg.h"
#include "migration/qemu-file.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "trace.h"

#define VIRTIO_MSG_VENDOR_ID 0x554D4551 /* 'QEMU' */

static bool virtio_msg_bad(VirtIOMSGProxy *s, VirtIOMSG *msg)
{
    bool drop = false;
    size_t min_size;
    unsigned int n;

    min_size = virtio_msg_header_size();
    switch (msg->msg_id) {
    case VIRTIO_MSG_GET_DEVICE_STATUS:
    case VIRTIO_MSG_DEVICE_INFO:
        break;
    case VIRTIO_MSG_GET_FEATURES:
        min_size += sizeof msg->get_features;
        break;
    case VIRTIO_MSG_SET_FEATURES:
        n = msg->set_features.num;

        /* We expect at least one feature block.  */
        if (n == 0 || n > VIRTIO_MSG_MAX_FEATURE_NUM) {
            drop = true;
            break;
        }

        min_size += sizeof msg->set_features + n * 4;
        break;
    case VIRTIO_MSG_GET_CONFIG:
        min_size += sizeof msg->get_config;
        break;
    case VIRTIO_MSG_SET_CONFIG:
        if (msg->set_config.size > VIRTIO_MSG_MAX_CONFIG_BYTES) {
            drop = true;
            break;
        }

        min_size += sizeof msg->set_config + msg->set_config.size;
        break;
    case VIRTIO_MSG_SET_DEVICE_STATUS:
        min_size += sizeof msg->set_device_status;
        break;
    case VIRTIO_MSG_GET_VQUEUE:
        min_size += sizeof msg->get_vqueue;
        break;
    case VIRTIO_MSG_SET_VQUEUE:
        min_size += sizeof msg->set_vqueue;
        break;
    case VIRTIO_MSG_RESET_VQUEUE:
        min_size += sizeof msg->reset_vqueue;
        break;
    case VIRTIO_MSG_EVENT_AVAIL:
        min_size += sizeof msg->event_avail;
        break;
    default:
        /* Unexpected message.  */
        drop = true;
        break;
    }

    /* Accept large messages allowing future backwards compatible extensions. */
    if (drop ||
        msg->msg_size < min_size || msg->msg_size > VIRTIO_MSG_MAX_SIZE) {
        return true;
    }

    if (msg->dev_num >= ARRAY_SIZE(s->devs)) {
        return true;
    }

    return false;
}

static VirtIODevice *virtio_msg_vdev(VirtIOMSGProxy *s, uint16_t dev_num)
{
    VirtIODevice *vdev;

    vdev = virtio_bus_get_device(&s->devs[dev_num].bus);
    return vdev;
}

static VirtIODevice *virtio_msg_lookup_vdev(VirtIOMSGProxy *s, uint16_t dev_num,
                                            const char *what)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, dev_num);

    if (!vdev) {
        error_report("%s: No virtio device on bus %s!",
                     what, BUS(&s->devs[dev_num].bus)->name);
    }

    return vdev;
}

static void virtio_msg_bus_get_devices(VirtIOMSGProxy *s,
                                       VirtIOMSG *msg)
{
    VirtIOMSG msg_resp;
    uint8_t data[VIRTIO_MSG_MAX_DEVS / 8] = {0};
    uint16_t req_offset = msg->bus_get_devices.offset;
    uint16_t offset = MIN(req_offset, VIRTIO_MSG_MAX_DEVS);
    uint16_t max_window = VIRTIO_MSG_MAX_DEVS - offset;
    uint16_t num = MIN(msg->bus_get_devices.num, max_window);
    uint16_t next_offset = offset + num;
    int i;

    for (i = 0; i < num; i++) {
        uint16_t dev_idx = offset + i;
        VirtIODevice *vdev = virtio_msg_vdev(s, dev_idx);

        if (vdev) {
            data[i / 8] |= 1U << (i & 7);
        }
    }

    virtio_msg_pack_bus_get_devices_resp(&msg_resp,
                                         offset, num, next_offset,
                                         data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_bus_ping(VirtIOMSGProxy *s, VirtIOMSG *msg)
{
    VirtIOMSG msg_resp;

    virtio_msg_pack_bus_ping_resp(&msg_resp, msg->bus_ping.data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_device_info(VirtIOMSGProxy *s,
                                   VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t config_len = 0;
    uint32_t device_id = 0;
    uint32_t max_vqs = 0;
    VirtIOMSG msg_resp;

    if (vdev) {
        device_id = vdev->device_id;
        config_len = vdev->config_len;
        max_vqs = virtio_get_num_queues(vdev);
    } else {
        error_report("%s: No virtio device on bus %s!",
                     __func__, BUS(&s->devs[msg->dev_num].bus)->name);
    }

    virtio_msg_pack_get_device_info_resp(&msg_resp, msg->dev_num, msg->token,
                                         device_id,
                                         VIRTIO_MSG_VENDOR_ID,
                                         /* Feature bits */
                                         64,
                                         config_len,
                                         max_vqs,
                                         0, 0);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_features(VirtIOMSGProxy *s,
                                    VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    VirtIOMSG msg_resp;
    uint32_t index = msg->get_features.index;
    uint32_t f[VIRTIO_MSG_MAX_FEATURE_NUM] = { 0 };
    uint32_t num = MIN(msg->get_features.num, VIRTIO_MSG_MAX_FEATURE_NUM);
    uint64_t features = 0;

    if (vdev) {
        VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

        features = vdev->host_features & ~vdc->legacy_features;
    }

    /* We only have 64 feature bits. If driver asks for more, return zeros  */
    if (index < 2) {
        features >>= index * 32;
        f[0] = features;
        f[1] = features >> 32;
    }

    /* If index is out of bounds, we respond with num=0, f=0.  */
    virtio_msg_pack_get_features_resp(&msg_resp, msg->dev_num, msg->token,
                                      index, num, f);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_set_features(VirtIOMSGProxy *s,
                                    VirtIOMSG *msg)
{
    VirtIOMSG msg_resp;
    unsigned int i;
    uint64_t f;

    f = s->devs[msg->dev_num].guest_features;

    for (i = 0; i < msg->set_features.num; i++) {
        unsigned int feature_index = i + msg->set_features.index;

        /* We only support up to 64bits  */
        if (feature_index >= 2) {
            break;
        }

        f = deposit64(f, feature_index * 32, 32, msg->set_features.b32[i]);
    }

    s->devs[msg->dev_num].guest_features = f;

    virtio_msg_pack_set_features_resp(&msg_resp, msg->dev_num, msg->token);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_soft_reset(VirtIOMSGProxy *s, uint16_t dev_num)
{
    assert(dev_num < ARRAY_SIZE(s->devs));

    virtio_bus_reset(&s->devs[dev_num].bus);
    s->devs[dev_num].guest_features = 0;
}

static void virtio_msg_set_device_status(VirtIOMSGProxy *s,
                                         VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t status = msg->set_device_status.status;
    VirtIOMSG msg_resp;

    if (!vdev) {
        return;
    }

    if (!(status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        virtio_bus_stop_ioeventfd(&s->devs[msg->dev_num].bus);
    }

    if (status & VIRTIO_CONFIG_S_FEATURES_OK) {
        virtio_set_features(vdev, s->devs[msg->dev_num].guest_features);
    }

    virtio_set_status(vdev, status);
    assert(vdev->status == status);

    if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
        virtio_bus_start_ioeventfd(&s->devs[msg->dev_num].bus);
    }

    if (status == 0) {
        virtio_msg_soft_reset(s, msg->dev_num);
    }

    virtio_msg_pack_set_device_status_resp(&msg_resp, msg->dev_num, msg->token,
                                           vdev->status);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_device_status(VirtIOMSGProxy *s,
                                         VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    VirtIOMSG msg_resp;
    uint32_t status = vdev ? vdev->status : 0;

    virtio_msg_pack_get_device_status_resp(&msg_resp, msg->dev_num, msg->token,
                                           status);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_config(VirtIOMSGProxy *s,
                                  VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    uint32_t size = msg->get_config.size;
    uint32_t offset = msg->get_config.offset;
    uint8_t data[VIRTIO_MSG_MAX_CONFIG_BYTES];
    VirtIOMSG msg_resp;
    uint32_t generation = 0;
    unsigned int i;

    if (size > VIRTIO_MSG_MAX_CONFIG_BYTES) {
        return;
    }

    memset(data, 0, size);

    if (vdev) {
        for (i = 0; i < size; i++) {
            data[i] = virtio_config_modern_readb(vdev, offset + i);
        }
        generation = vdev->generation;
    }

    virtio_msg_pack_get_config_resp(&msg_resp, msg->dev_num, msg->token,
                                    size, offset,
                                    generation, data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_set_config(VirtIOMSGProxy *s,
                                  VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    uint32_t offset = msg->set_config.offset;
    uint32_t size = msg->set_config.size;
    uint8_t *data = msg->set_config.data;
    VirtIOMSG msg_resp;
    uint32_t generation = 0;
    unsigned int i;

    if (vdev) {
        for (i = 0; i < size; i++) {
            virtio_config_modern_writeb(vdev, offset + i, data[i]);
        }
        generation = vdev->generation;
    }

    virtio_msg_pack_set_config_resp(&msg_resp, msg->dev_num, msg->token,
                                    size, offset,
                                    generation, data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_vqueue(VirtIOMSGProxy *s,
                                  VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    uint32_t max_size = 0;
    uint32_t index = msg->get_vqueue.index;
    hwaddr desc = 0, avail = 0, used = 0;
    VirtIOMSG msg_resp;
    uint32_t size = 0;

    if (index < VIRTIO_QUEUE_MAX && vdev) {
        max_size = virtio_queue_get_max_num(vdev, index);
        size = virtio_queue_get_num(vdev, index);
        if (size) {
            virtio_queue_get_rings(vdev, index, &desc, &avail, &used);
        }
        virtio_msg_pack_get_vqueue_resp(&msg_resp, msg->dev_num, msg->token,
                                        index, max_size, size,
                                        desc, avail, used);
    } else {
        /* OOB index or missing device, respond with all zeroes. */
        virtio_msg_pack_get_vqueue_resp(&msg_resp, msg->dev_num, msg->token,
                                        index, 0, 0, 0, 0, 0);
    }

    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_set_vqueue(VirtIOMSGProxy *s, VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    uint32_t index = msg->set_vqueue.index;
    VirtIOMSG msg_resp;

    if (!vdev || index >= VIRTIO_QUEUE_MAX) {
        /* Missing device or OOB index, ignore. */
        return;
    }

    virtio_queue_set_vector(vdev, index, index);
    virtio_queue_set_num(vdev, index, msg->set_vqueue.size);
    virtio_queue_set_rings(vdev, index,
                           msg->set_vqueue.descriptor_addr,
                           msg->set_vqueue.driver_addr,
                           msg->set_vqueue.device_addr);
    virtio_queue_enable(vdev, index);

    virtio_msg_pack_set_vqueue_resp(&msg_resp, msg->dev_num, msg->token);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_reset_vqueue(VirtIOMSGProxy *s, VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    VirtIOMSG msg_resp;

    if (!vdev) {
        return;
    }

    virtio_queue_reset(vdev, msg->reset_vqueue.index);

    virtio_msg_pack_reset_vqueue_resp(&msg_resp, msg->dev_num, msg->token);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_event_avail(VirtIOMSGProxy *s,
                                   VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, msg->dev_num, __func__);
    uint16_t vq_idx = msg->event_avail.index;
    VirtQueue *vq;

    if (!vdev) {
        return;
    }

    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        VirtIOMSG msg_ev;

        virtio_error(vdev, "Notification while driver not OK?");
        virtio_msg_pack_event_config(&msg_ev, msg->dev_num,
                                     vdev->status, vdev->generation,
                                     0, 0, NULL);
        virtio_msg_bus_send(&s->msg_bus, &msg_ev);
        return;
    }

    if (vq_idx >= VIRTIO_QUEUE_MAX) {
        virtio_error(vdev, "Notification to bad VQ!");
        return;
    }

    if (!virtio_queue_get_num(vdev, vq_idx)) {
        virtio_error(vdev, "Notification to unconfigured VQ!");
        return;
    }

    vq = virtio_get_queue(vdev, vq_idx);
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_NOTIFICATION_DATA)) {
        uint32_t next_offset_wrap = msg->event_avail.next_offset_wrap;
        uint16_t qsize = virtio_queue_get_num(vdev, vq_idx);
        uint32_t offset = next_offset_wrap & 0x7fffffff;
        bool wrap = next_offset_wrap & 0x80000000;
        uint16_t data;

        if (offset > 0x7fff || offset >= qsize) {
            virtio_error(vdev, "Next offset to large!");
            /* Bail out without notification???  */
            return;
        }

        data = wrap << 15;
        data |= offset & 0x7fff;

        virtio_queue_set_shadow_avail_idx(vq, data);
    }
    virtio_queue_notify(vdev, msg->event_avail.index);
}

typedef void (*VirtIOMSGHandler)(VirtIOMSGProxy *s,
                                 VirtIOMSG *msg);

static const VirtIOMSGHandler msg_handlers[] = {
    [VIRTIO_MSG_DEVICE_INFO] = virtio_msg_device_info,
    [VIRTIO_MSG_GET_FEATURES] = virtio_msg_get_features,
    [VIRTIO_MSG_SET_FEATURES] = virtio_msg_set_features,
    [VIRTIO_MSG_GET_DEVICE_STATUS] = virtio_msg_get_device_status,
    [VIRTIO_MSG_SET_DEVICE_STATUS] = virtio_msg_set_device_status,
    [VIRTIO_MSG_GET_CONFIG] = virtio_msg_get_config,
    [VIRTIO_MSG_SET_CONFIG] = virtio_msg_set_config,
    [VIRTIO_MSG_GET_VQUEUE] = virtio_msg_get_vqueue,
    [VIRTIO_MSG_SET_VQUEUE] = virtio_msg_set_vqueue,
    [VIRTIO_MSG_RESET_VQUEUE] = virtio_msg_reset_vqueue,
    [VIRTIO_MSG_EVENT_AVAIL] = virtio_msg_event_avail,
};

static int virtio_msg_receive_msg(VirtIOMSGBusDevice *bd, VirtIOMSG *msg)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(bd->opaque);
    VirtIOMSGHandler handler;

    /* virtio_msg_print(msg); */

    /* We handle some generic bus messages. */
    if (msg->type & VIRTIO_MSG_TYPE_BUS) {
        if (msg->msg_id == VIRTIO_MSG_BUS_GET_DEVICES) {
            virtio_msg_bus_get_devices(s, msg);
        }
        if (msg->msg_id == VIRTIO_MSG_BUS_PING) {
            virtio_msg_bus_ping(s, msg);
        }
        return VIRTIO_MSG_NO_ERROR;
    }

    if (msg->msg_id >= ARRAY_SIZE(msg_handlers)) {
        return VIRTIO_MSG_ERROR_UNSUPPORTED_MESSAGE_ID;
    }

    handler = msg_handlers[msg->msg_id];

    /* We don't expect responses.  */
    if ((msg->type & VIRTIO_MSG_TYPE_RESPONSE) || virtio_msg_bad(s, msg)) {
        /* Drop bad messages.  */
        return VIRTIO_MSG_ERROR_BAD_MESSAGE;
    }

    if (handler) {
        handler(s, msg);
    }

    return VIRTIO_MSG_NO_ERROR;
}

static const VirtIOMSGBusPort virtio_msg_port = {
    .receive = virtio_msg_receive_msg,
    .is_driver = false
};

static void virtio_msg_notify(DeviceState *opaque, uint16_t vector)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, mdev->dev_num, __func__);
    VirtIOMSG msg;

    if (!vdev || !virtio_msg_bus_connected(&s->msg_bus)) {
        return;
    }

    if (vector < VIRTIO_QUEUE_MAX) {
        virtio_msg_pack_event_used(&msg, mdev->dev_num, vector);
        virtio_msg_bus_send(&s->msg_bus, &msg);
        return;
    }

    if (vector < VIRTIO_NO_VECTOR) {
        virtio_msg_pack_event_config(&msg, mdev->dev_num,
                                     vdev->status, vdev->generation,
                                     0, 0, NULL);
        virtio_msg_bus_send(&s->msg_bus, &msg);
        return;
    }
}

static void virtio_msg_save_queue(DeviceState *opaque, int n, QEMUFile *f)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_vdev(s, mdev->dev_num);
    uint16_t vector = VIRTIO_NO_VECTOR;

    if (vdev) {
        vector = virtio_queue_vector(vdev, n);
    }

    /* Preserve per-queue MSI-X vector so notifications keep working. */
    qemu_put_be16(f, vector);
}

static int virtio_msg_load_queue(DeviceState *opaque, int n, QEMUFile *f)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_vdev(s, mdev->dev_num);
    uint16_t vector;

    /* Restore the MSI-X vector saved by virtio_msg_save_queue(). */
    qemu_get_be16s(f, &vector);

    if (!vdev) {
        return -ENODEV;
    }

    if (vector != VIRTIO_NO_VECTOR && vector >= VIRTIO_QUEUE_MAX) {
        return -EINVAL;
    }

    virtio_queue_set_vector(vdev, n, vector);
    return 0;
}

static bool virtio_msg_has_vdevs(VirtIOMSGProxy *s)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->devs); i++) {
        if (virtio_msg_vdev(s, i)) {
            return true;
        }
    }

    return false;
}

static void virtio_msg_connect_bus(VirtIOMSGProxy *s, bool has_vdevs)
{
    if (!has_vdevs) {
        return;
    }

    if (!virtio_msg_bus_connect(&s->msg_bus, &virtio_msg_port, s)) {
        /* This is a user error, forgetting to setup a msg-bus.  */
        error_report("%s: No bus connected!",
                     object_get_canonical_path(OBJECT(s)));
        exit(EXIT_FAILURE);
    }
}

static int virtio_msg_post_load(void *opaque, int version_id)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(opaque);

    virtio_msg_connect_bus(s, virtio_msg_has_vdevs(s));
    return 0;
}

static void virtio_msg_vmstate_change(DeviceState *d, bool running)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(d);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIOMSGBusDevice *bd;

    if (!running) {
        return;
    }

    if (!virtio_msg_bus_connected(&s->msg_bus)) {
        return;
    }

    bd = virtio_msg_bus_get_device(&s->msg_bus);
    if (!bd) {
        return;
    }

    /* Resume path: ensure any pending bus work is processed post-migration. */
    virtio_msg_bus_process(bd);
}

static const VMStateDescription vmstate_virtio_msg_dev = {
    .name = "virtio_msg/dev",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(guest_features, VirtIOMSGDev),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_msg_state_sub = {
    .name = "virtio_msg_proxy_backend/state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(devs, VirtIOMSGProxy, VIRTIO_MSG_MAX_DEVS, 0,
                             vmstate_virtio_msg_dev, VirtIOMSGDev),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_msg = {
    .name = "virtio_msg_proxy_backend",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_msg_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_virtio_msg_state_sub,
        NULL
    }
};

static void virtio_msg_save_extra_state(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);

    vmstate_save_state(f, &vmstate_virtio_msg, s, NULL, &error_fatal);
}

static int virtio_msg_load_extra_state(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);

    return vmstate_load_state(f, &vmstate_virtio_msg, s, 1, &error_fatal);
}

static bool virtio_msg_has_extra_state(DeviceState *opaque)
{
    return true;
}

static void virtio_msg_reset_hold(Object *obj, ResetType type)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(obj);
    VirtIODevice *vdev;
    bool found_a_vdev = false;
    int i;

    for (i = 0; i < ARRAY_SIZE(s->devs); i++) {
        virtio_msg_soft_reset(s, i);

        vdev = virtio_msg_vdev(s, i);
        if (vdev) {
            found_a_vdev = true;
        }
    }

    /* Only connect transports with virtio-devs.  */
    virtio_msg_connect_bus(s, found_a_vdev);
}

static bool virtio_msg_ioeventfd_enabled(DeviceState *d)
{
    /* We don't have any MMIO/PIO regs directly mapped to eventfds.  */
    return false;
}

static int virtio_msg_ioeventfd_assign(DeviceState *d,
                                        EventNotifier *notifier,
                                        int n, bool assign)
{
    /*
     * virtio-msg has no MMIO/PIO notify register to bind an ioeventfd to.
     * Host kicks arrive via EVENT_AVAIL messages, and we explicitly signal
     * the per-queue host notifier in virtio_msg_event_avail().
     * Nothing to map here; return success so vhost can proceed.
     */
    return 0;
}

static int virtio_msg_set_guest_notifier(DeviceState *d, int n, bool assign,
                                          bool with_irqfd)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(d);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, mdev->dev_num, __func__);
    VirtQueue *vq;
    EventNotifier *notifier;
    VirtioDeviceClass *vdc;

    if (!vdev) {
        return -ENODEV;
    }

    vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    vq = virtio_get_queue(vdev, n);
    notifier = virtio_queue_get_guest_notifier(vq);

    if (assign) {
        int r = event_notifier_init(notifier, 0);
        if (r < 0) {
            return r;
        }
        virtio_queue_set_guest_notifier_fd_handler(vq, true, with_irqfd);
    } else {
        virtio_queue_set_guest_notifier_fd_handler(vq, false, with_irqfd);
        event_notifier_cleanup(notifier);
    }

    if (vdc->guest_notifier_mask && vdev->use_guest_notifier_mask) {
        vdc->guest_notifier_mask(vdev, n, !assign);
    }

    return 0;
}

static int virtio_msg_set_config_guest_notifier(DeviceState *d, bool assign,
                                                 bool with_irqfd)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(d);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, mdev->dev_num, __func__);
    EventNotifier *notifier;
    VirtioDeviceClass *vdc;
    int r = 0;

    if (!vdev) {
        return -ENODEV;
    }

    vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    notifier = virtio_config_get_guest_notifier(vdev);

    if (assign) {
        r = event_notifier_init(notifier, 0);
        if (r < 0) {
            return r;
        }
        virtio_config_set_guest_notifier_fd_handler(vdev, assign, with_irqfd);
    } else {
        virtio_config_set_guest_notifier_fd_handler(vdev, assign, with_irqfd);
        event_notifier_cleanup(notifier);
    }
    if (vdc->guest_notifier_mask && vdev->use_guest_notifier_mask) {
        vdc->guest_notifier_mask(vdev, VIRTIO_CONFIG_IRQ_IDX, !assign);
    }
    return r;
}

static int virtio_msg_set_guest_notifiers(DeviceState *d, int nvqs,
                                          bool assign)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(d);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, mdev->dev_num, __func__);
    /* Mirror virtio-mmio: use eventfd handlers and skip irqfd for now. */
    bool with_irqfd = false;
    int r, n;

    if (!vdev) {
        return -ENODEV;
    }

    nvqs = MIN(nvqs, VIRTIO_QUEUE_MAX);

    for (n = 0; n < nvqs; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }

        r = virtio_msg_set_guest_notifier(d, n, assign, with_irqfd);
        if (r < 0) {
            goto assign_error;
        }
    }
    r = virtio_msg_set_config_guest_notifier(d, assign, with_irqfd);
    if (r < 0) {
        goto assign_error;
    }

    return 0;

assign_error:
    /* We get here on assignment failure. Recover by undoing for VQs 0 .. n. */
    assert(assign);
    while (--n >= 0) {
        virtio_msg_set_guest_notifier(d, n, !assign, false);
    }
    return r;
}

static void virtio_msg_pre_plugged(DeviceState *d, Error **errp)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(d);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_lookup_vdev(s, mdev->dev_num, __func__);

    if (!vdev) {
        return;
    }

    virtio_add_feature(&vdev->host_features, VIRTIO_F_VERSION_1);
}

static AddressSpace *virtio_msg_get_dma_as(DeviceState *d)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(d);
    AddressSpace *as;

    as = virtio_msg_bus_get_remote_as(&s->msg_bus);
    return as;
}

static int virtio_msg_query_nvectors(DeviceState *d)
{
    return VIRTIO_QUEUE_MAX;
}

static void virtio_msg_realize(DeviceState *d, Error **errp)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(d);
    Object *o = OBJECT(d);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->devs); i++) {
        g_autofree char *outer_bus_name = g_strdup_printf("bus%d", i);

        qbus_init(&s->devs_bus[i], sizeof(s->devs_bus[i]),
                  TYPE_VIRTIO_MSG_OUTER_BUS, d, outer_bus_name);

        object_initialize_child(o, "dev[*]", &s->devs[i], TYPE_VIRTIO_MSG_DEV);
        s->devs[i].proxy = s;
        s->devs[i].dev_num = i;
        qdev_realize(DEVICE(&s->devs[i]), BUS(&s->devs_bus[i]), &error_fatal);

        qbus_init(&s->devs[i].bus, sizeof(s->devs[i].bus),
                  TYPE_VIRTIO_MSG_PROXY_BUS, DEVICE(&s->devs[i]), "bus");
    }

    qbus_init(&s->msg_bus, sizeof(s->msg_bus),
              TYPE_VIRTIO_MSG_BUS, d, "msg-bus");
}

static void virtio_msg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = virtio_msg_realize;
    dc->bus_type = TYPE_VIRTIO_MSG_OUTER_BUS;
    dc->user_creatable = true;
    rc->phases.hold = virtio_msg_reset_hold;
}

static void virtio_msg_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_VIRTIO_MSG_OUTER_BUS;
}

static const TypeInfo virtio_msg_types[] = {
    {
        .name          = TYPE_VIRTIO_MSG,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(VirtIOMSGProxy),
        .class_init    = virtio_msg_class_init,
    },
    {
        .name          = TYPE_VIRTIO_MSG_DEV,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(VirtIOMSGDev),
        .class_init    = virtio_msg_dev_class_init,
    },
};
DEFINE_TYPES(virtio_msg_types);

static char *virtio_msg_bus_get_dev_path(DeviceState *dev)
{
    BusState *bus = qdev_get_parent_bus(dev);
    return strdup(object_get_canonical_path(OBJECT(bus->parent)));
}

static void virtio_msg_bus_class_init(ObjectClass *klass, const void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);

    k->notify = virtio_msg_notify;
    k->save_queue = virtio_msg_save_queue;
    k->load_queue = virtio_msg_load_queue;
    k->save_extra_state = virtio_msg_save_extra_state;
    k->load_extra_state = virtio_msg_load_extra_state;
    k->has_extra_state = virtio_msg_has_extra_state;
    k->pre_plugged = virtio_msg_pre_plugged;
    k->has_variable_vring_alignment = true;
    k->get_dma_as = virtio_msg_get_dma_as;
    k->query_nvectors = virtio_msg_query_nvectors;

    k->set_guest_notifiers = virtio_msg_set_guest_notifiers;
    k->ioeventfd_enabled = virtio_msg_ioeventfd_enabled;
    k->ioeventfd_assign = virtio_msg_ioeventfd_assign;
    k->vmstate_change = virtio_msg_vmstate_change;

    /* Needed for multiple devs of the same kind (virtio-net).  */
    bus_class->get_dev_path = virtio_msg_bus_get_dev_path;
}

static const TypeInfo virtio_msg_bus_types[] = {
    {
        /* Specialized virtio-bus with our custom callbacks.  */
        .name          = TYPE_VIRTIO_MSG_PROXY_BUS,
        .parent        = TYPE_VIRTIO_BUS,
        .instance_size = sizeof(VirtioBusState),
        .class_init    = virtio_msg_bus_class_init,
    },
    {
        /*
         * Outer bus to hold virtio-msg objects making them visible to the
         * qom-tree.
         */
        .name          = TYPE_VIRTIO_MSG_OUTER_BUS,
        .parent        = TYPE_BUS,
        .instance_size = sizeof(BusState),
    },
};

DEFINE_TYPES(virtio_msg_bus_types);
