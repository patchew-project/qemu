/*
 * A virtio device implementing a PONG device
 *
 * Copyright 2020 IBM.
 * Copyright 2020 Pierre Morel <pmorel@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "hw/virtio/virtio.h"
#include "hw/qdev-properties.h"
#include "standard-headers/linux/virtio_ids.h"
#include "hw/virtio/virtio-pong.h"
#include "qom/object_interfaces.h"
#include "trace.h"
#include "qemu/error-report.h"

static char *buffer;
static unsigned int cksum;

static unsigned int simple_checksum(char *buf, unsigned long len)
{
    unsigned int sum = 0;

    while (len--) {
        sum += *buf * *buf + 7 * *buf + 3;
        buf++;
    }
    return sum;
}

static void handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOPONG *vpong = VIRTIO_PONG(vdev);
    VirtQueueElement *elem;

    if (!virtio_queue_ready(vq)) {
        return;
    }
    if (virtio_queue_empty(vq)) {
        return;
    }

    while ((elem = virtqueue_pop(vq, sizeof(*elem))) != NULL) {
        buffer = g_malloc(elem->out_sg->iov_len);
        iov_to_buf(elem->out_sg, elem->out_num, 0, buffer,
                   elem->out_sg->iov_len);

        if (vpong->cksum) {
            cksum = simple_checksum(buffer, elem->out_sg->iov_len);
        }
        virtqueue_push(vq, elem, 0);
        g_free(buffer);
        g_free(elem);
    }

    virtio_notify(vdev, vq);
}

static void handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;

    if (!virtio_queue_ready(vq)) {
        return;
    }
    if (virtio_queue_empty(vq)) {
        return;
    }

    while ((elem = virtqueue_pop(vq, sizeof(*elem))) != NULL) {
        int len = 0;

        len = iov_from_buf(elem->out_sg, elem->out_num,
                         0, &cksum, sizeof(cksum));

        virtqueue_push(vq, elem, len);
        g_free(elem);
    }

    virtio_notify(vdev, vq);

}

static uint64_t get_features(VirtIODevice *vdev, uint64_t f, Error **errp)
{
    VirtIOPONG *vpong = VIRTIO_PONG(vdev);

    if (vpong->cksum) {
        f |= 1ull << VIRTIO_PONG_F_CKSUM;
    }
    return f;
}

static void virtio_pong_set_status(VirtIODevice *vdev, uint8_t status)
{
    if (!vdev->vm_running) {
        return;
    }
    vdev->status = status;
}

static void virtio_pong_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOPONG *vpong = VIRTIO_PONG(dev);

    virtio_init(vdev, "virtio-pong", VIRTIO_ID_PONG, 0);

    vpong->vq_in = virtio_add_queue(vdev, 8, handle_input);
    vpong->vq_out = virtio_add_queue(vdev, 8, handle_output);
}

static void virtio_pong_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOPONG *vpong = VIRTIO_PONG(dev);

    qemu_del_vm_change_state_handler(vpong->vmstate);
    virtio_del_queue(vdev, 0);
    virtio_cleanup(vdev);
}

static Property virtio_pong_properties[] = {
    DEFINE_PROP_UINT64("cksum", VirtIOPONG, cksum, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_pong_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_pong_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_pong_device_realize;
    vdc->unrealize = virtio_pong_device_unrealize;
    vdc->get_features = get_features;
    vdc->set_status = virtio_pong_set_status;
}

static const TypeInfo virtio_pong_info = {
    .name = TYPE_VIRTIO_PONG,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOPONG),
    .class_init = virtio_pong_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_pong_info);
}

type_init(virtio_register_types)
