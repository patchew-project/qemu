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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-msg-bus.h"
#include "hw/virtio/virtio-msg.h"
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

static void virtio_msg_bus_get_devices(VirtIOMSGProxy *s,
                                       VirtIOMSG *msg)
{
    VirtIODevice *vdev;
    VirtIOMSG msg_resp;
    uint8_t data[VIRTIO_MSG_MAX_DEVS / 8] = {0};
    uint16_t num = MAX(msg->bus_get_devices.num, VIRTIO_MSG_MAX_DEVS);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->devs); i++) {
        vdev = virtio_msg_vdev(s, i);
        if (vdev) {
            data[i / 8] |= 1U << (i & 7);
        }
    }

    virtio_msg_pack_bus_get_devices_resp(&msg_resp,
                                         msg->bus_get_devices.offset, num, 0,
                                         data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_device_info(VirtIOMSGProxy *s,
                                   VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t config_len = 0;
    uint32_t device_id = 0;
    VirtIOMSG msg_resp;

    if (vdev) {
        device_id = vdev->device_id;
        config_len = vdev->config_len;
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
                                         VIRTIO_QUEUE_MAX,
                                         0, 0);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_features(VirtIOMSGProxy *s,
                                    VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    VirtIOMSG msg_resp;
    uint32_t index = msg->get_features.index;
    uint32_t f[VIRTIO_MSG_MAX_FEATURE_NUM] = { 0 };
    uint32_t num = msg->get_features.num;
    uint64_t features;

    features = vdev->host_features & ~vdc->legacy_features;

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
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    VirtIOMSG msg_resp;

    virtio_msg_pack_get_device_status_resp(&msg_resp, msg->dev_num, msg->token,
                                           vdev->status);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_config(VirtIOMSGProxy *s,
                                  VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t size = msg->get_config.size;
    uint32_t offset = msg->get_config.offset;
    uint8_t data[VIRTIO_MSG_MAX_CONFIG_BYTES];
    VirtIOMSG msg_resp;
    unsigned int i;

    if (size > VIRTIO_MSG_MAX_CONFIG_BYTES) {
        return;
    }

    for (i = 0; i < size; i++) {
        data[i] = virtio_config_modern_readb(vdev, offset + i);
    }

    virtio_msg_pack_get_config_resp(&msg_resp, msg->dev_num, msg->token,
                                    size, offset,
                                    vdev->generation, data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_set_config(VirtIOMSGProxy *s,
                                  VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t offset = msg->set_config.offset;
    uint32_t size = msg->set_config.size;
    uint8_t *data = msg->set_config.data;
    VirtIOMSG msg_resp;
    unsigned int i;

    for (i = 0; i < size; i++) {
        virtio_config_modern_writeb(vdev, offset + i, data[i]);
    }

    virtio_msg_pack_set_config_resp(&msg_resp, msg->dev_num, msg->token,
                                    size, offset,
                                    vdev->generation, data);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_get_vqueue(VirtIOMSGProxy *s,
                                  VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t max_size = VIRTQUEUE_MAX_SIZE;
    uint32_t index = msg->get_vqueue.index;
    hwaddr desc, avail, used;
    VirtIOMSG msg_resp;
    uint32_t size;

    if (index < VIRTIO_QUEUE_MAX) {
        size = virtio_queue_get_num(vdev, index);
        if (!size) {
            max_size = 0;
        }

        virtio_queue_get_rings(vdev, index, &desc, &avail, &used);
        virtio_msg_pack_get_vqueue_resp(&msg_resp, msg->dev_num, msg->token,
                                        index, max_size, size,
                                        desc, avail, used);
    } else {
        /* OOB index, respond with all zeroes. */
        virtio_msg_pack_get_vqueue_resp(&msg_resp, msg->dev_num, msg->token,
                                        index, 0, 0, 0, 0, 0);
    }

    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_set_vqueue(VirtIOMSGProxy *s, VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint32_t index = msg->set_vqueue.index;
    VirtIOMSG msg_resp;

    if (index >= VIRTIO_QUEUE_MAX) {
        /* OOB index, ignore. */
        return;
    }

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
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    VirtIOMSG msg_resp;

    virtio_queue_reset(vdev, msg->reset_vqueue.index);

    virtio_msg_pack_reset_vqueue_resp(&msg_resp, msg->dev_num, msg->token);
    virtio_msg_bus_send(&s->msg_bus, &msg_resp);
}

static void virtio_msg_event_avail(VirtIOMSGProxy *s,
                                   VirtIOMSG *msg)
{
    VirtIODevice *vdev = virtio_msg_vdev(s, msg->dev_num);
    uint16_t vq_idx = msg->event_avail.index;

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

    /* NOTIFICATION_DATA doesn't exist in QEMU 8.2.7. if (0) it */
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_NOTIFICATION_DATA) && 0) {
        VirtQueue *vq = virtio_get_queue(vdev, vq_idx);
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
            return VIRTIO_MSG_NO_ERROR;
        }
    }

    if (msg->msg_id > ARRAY_SIZE(msg_handlers)) {
        return VIRTIO_MSG_ERROR_UNSUPPORTED_MESSAGE_ID;
    }

    handler = msg_handlers[msg->msg_id];
    assert((msg->type & VIRTIO_MSG_TYPE_RESPONSE) == 0);

    if (virtio_msg_bad(s, msg)) {
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

static void virtio_msg_notify_queue(DeviceState *opaque, uint16_t index)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_vdev(s, mdev->dev_num);
    VirtIOMSG msg;

    if (!vdev || !virtio_msg_bus_connected(&s->msg_bus)) {
        return;
    }

    virtio_msg_pack_event_used(&msg, mdev->dev_num, index);
    virtio_msg_bus_send(&s->msg_bus, &msg);
}

static void virtio_msg_notify(DeviceState *opaque, uint16_t vector)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(opaque);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_vdev(s, mdev->dev_num);
    VirtIOMSG msg;

    if (!virtio_msg_bus_connected(&s->msg_bus)) {
        return;
    }

    /* Check if we're notifying for VQ or CONFIG updates.  */
    if (vdev->isr & 2) {
        virtio_msg_pack_event_config(&msg, mdev->dev_num,
                                     vdev->status, vdev->generation,
                                     0, 0, NULL);
        virtio_msg_bus_send(&s->msg_bus, &msg);
    }
}

static const VMStateDescription vmstate_virtio_msg_state_sub = {
    .name = "virtio_msg_device",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_msg = {
    .name = "virtio_msg_proxy_backend",
    .version_id = 1,
    .minimum_version_id = 1,
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
    VirtIOMSGProxy *s = VIRTIO_MSG(opaque);

    vmstate_save_state(f, &vmstate_virtio_msg, s, NULL, &error_fatal);
}

static int virtio_msg_load_extra_state(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(opaque);

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
    bool found_a_vdev;
    int i;

    for (i = 0; i < ARRAY_SIZE(s->devs); i++) {
        virtio_msg_soft_reset(s, i);

        vdev = virtio_msg_vdev(s, i);
        if (vdev) {
            found_a_vdev = true;
        }
    }

    /* Only connect transports with virtio-devs.  */
    if (found_a_vdev) {
        bool r;

        r = virtio_msg_bus_connect(&s->msg_bus, &virtio_msg_port, s);
        if (!r) {
            /* This is a user error, forgetting to setup a msg-bus.  */
            error_report("%s: No bus connected!",
                         object_get_canonical_path(obj));
            exit(EXIT_FAILURE);
        }
    }
}

static void virtio_msg_pre_plugged(DeviceState *d, Error **errp)
{
    VirtIOMSGDev *mdev = VIRTIO_MSG_DEV(d);
    VirtIOMSGProxy *s = VIRTIO_MSG(mdev->proxy);
    VirtIODevice *vdev = virtio_msg_vdev(s, mdev->dev_num);

    virtio_add_feature(&vdev->host_features, VIRTIO_F_VERSION_1);
}

static AddressSpace *virtio_msg_get_dma_as(DeviceState *d)
{
    VirtIOMSGProxy *s = VIRTIO_MSG(d);
    AddressSpace *as;

    as = virtio_msg_bus_get_remote_as(&s->msg_bus);
    return as;
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
        .instance_size = sizeof(DeviceState),
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

    k->notify_queue = virtio_msg_notify_queue;
    k->notify = virtio_msg_notify;
    k->save_extra_state = virtio_msg_save_extra_state;
    k->load_extra_state = virtio_msg_load_extra_state;
    k->has_extra_state = virtio_msg_has_extra_state;
    k->pre_plugged = virtio_msg_pre_plugged;
    k->has_variable_vring_alignment = true;
    k->get_dma_as = virtio_msg_get_dma_as;

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
