/*
 * vhost-pci-net support
 *
 * Copyright Intel, Inc. 2016
 *
 * Authors:
 *  Wei Wang <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-pci-net.h"

#define VPNET_CTRLQ_SIZE 32
#define VPNET_VQ_SIZE 256

static void vpnet_handle_vq(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vpnet_handle_ctrlq(VirtIODevice *vdev, VirtQueue *vq)
{
}

/* Send a ctrlq message to the driver */
static size_t vpnet_send_ctrlq_msg(VhostPCINet *vpnet,
                                 struct vpnet_ctrlq_msg *msg)
{
    VirtQueueElement *elem;
    VirtQueue *vq;
    size_t msg_len = msg->size;

    vq = vpnet->ctrlq;
    if (!virtio_queue_ready(vq)) {
        return 0;
    }

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        return 0;
    }

    iov_from_buf(elem->in_sg, elem->in_num, 0, msg, msg_len);

    virtqueue_push(vq, elem, msg_len);
    virtio_notify(VIRTIO_DEVICE(vpnet), vq);
    g_free(elem);

    return msg_len;
}

/* Send a ctrlq message of the remote memory to the driver */
static void vpnet_send_ctrlq_msg_remote_mem(VhostPCINet *vpnet)
{
    VhostPCIDev *vp_dev = get_vhost_pci_dev();
    struct vpnet_ctrlq_msg *msg;
    uint16_t payload_size, msg_size;

    payload_size = vp_dev->remote_mem_num *
                   sizeof(struct ctrlq_msg_remote_mem);
    msg_size = payload_size + VPNET_CTRLQ_MSG_HDR_SIZE;
    msg = g_malloc(msg_size);
    msg->class = VHOST_PCI_CTRLQ_MSG_REMOTE_MEM;
    msg->size = msg_size;
    memcpy(msg->payload.msg_remote_mem, vp_dev->remote_mem, payload_size);
    vpnet_send_ctrlq_msg(vpnet, msg);
    g_free(msg);
}

static void vpnet_ctrlq_msg_remoteq_add_one(struct vpnet_ctrlq_msg *msg,
                                            Remoteq *remoteq)
{
    uint32_t vring_num = remoteq->vring_num;
    struct ctrlq_msg_remoteq *msg_remoteq;

    msg_remoteq = &msg->payload.msg_remoteq[vring_num];
    msg_remoteq->last_avail_idx = remoteq->last_avail_idx;
    msg_remoteq->vring_num = vring_num;
    msg_remoteq->vring_enable = remoteq->enabled;
    msg_remoteq->desc_gpa = remoteq->addr.desc_user_addr;
    msg_remoteq->avail_gpa = remoteq->addr.avail_user_addr;
    msg_remoteq->used_gpa = remoteq->addr.used_user_addr;
}

/* Send a ctrlq message of the remoteq info to the driver */
static void vpnet_send_ctrlq_msg_remoteq(VhostPCINet *vpnet)
{
    Remoteq *remoteq;
    struct vpnet_ctrlq_msg *msg;
    uint16_t remoteq_num, msg_size;
    VhostPCIDev *vp_dev = get_vhost_pci_dev();

    remoteq_num = vp_dev->remoteq_num;
    msg_size = sizeof(struct ctrlq_msg_remoteq) * remoteq_num +
               VPNET_CTRLQ_MSG_HDR_SIZE;
    msg = g_malloc(msg_size);
    msg->class = VHOST_PCI_CTRLQ_MSG_REMOTEQ;
    msg->size = msg_size;

    QLIST_FOREACH(remoteq, &vp_dev->remoteq_list, node) {
        /* Get remoteqs from the list, and fill them into the ctrlq_msg */
        vpnet_ctrlq_msg_remoteq_add_one(msg, remoteq);
    }

    vpnet_send_ctrlq_msg(vpnet, msg);
    g_free(msg);
}

static void vpnet_set_status(struct VirtIODevice *vdev, uint8_t status)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);

    /* Send the ctrlq messages to the driver when the ctrlq is ready */
    if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
        vpnet_send_ctrlq_msg_remote_mem(vpnet);
        vpnet_send_ctrlq_msg_remoteq(vpnet);
    }
}

static uint64_t vpnet_get_features(VirtIODevice *vdev, uint64_t features,
                                   Error **errp)
{
    VhostPCIDev *vp_dev = get_vhost_pci_dev();

    /*
     * Give the driver the feature bits that have been negotiated with the
     * remote device.
     */
    return vp_dev->feature_bits;
}

static void vpnet_set_features(VirtIODevice *vdev, uint64_t features)
{
}

static void vpnet_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    struct vhost_pci_net_config netcfg;

    virtio_stw_p(vdev, &netcfg.status, vpnet->status);
    virtio_stw_p(vdev, &netcfg.vq_pairs, vpnet->vq_pairs);
    memcpy(config, &netcfg, vpnet->config_size);
}

static void vpnet_set_config(VirtIODevice *vdev, const uint8_t *config)
{
}

static void vpnet_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    uint16_t i, vq_num;
    VhostPCIDev *vp_dev = get_vhost_pci_dev();

    vq_num = vp_dev->remoteq_num;
    vpnet->vq_pairs = vq_num / 2;
    virtio_init(vdev, "vhost-pci-net", VIRTIO_ID_VHOST_PCI_NET,
                vpnet->config_size);

    /* Add local vqs */
    for (i = 0; i < vq_num; i++) {
        virtio_add_queue(vdev, VPNET_VQ_SIZE, vpnet_handle_vq);
    }
    /* Add the ctrlq */
    vpnet->ctrlq = virtio_add_queue(vdev, VPNET_CTRLQ_SIZE, vpnet_handle_ctrlq);

    vpnet->status = 0;
    vp_dev->vdev = vdev;
}

static void vpnet_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    uint16_t i, vq_num = vpnet->vq_pairs * 2;

    /* Delete the datapath vqs and the ctrlq */
    for (i = 0; i < vq_num + 1; i++) {
        virtio_del_queue(vdev, i);
    }
}

static void vpnet_reset(VirtIODevice *vdev)
{
}

static Property vpnet_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void vpnet_instance_init(Object *obj)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(obj);

    /*
     * The default config_size is sizeof(struct vhost_pci_net_config).
     * Can be overriden with vpnet_set_config_size.
     */
    vpnet->config_size = sizeof(struct vhost_pci_net_config);
}

static void vpnet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vpnet_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = vpnet_device_realize;
    vdc->unrealize = vpnet_device_unrealize;
    vdc->get_config = vpnet_get_config;
    vdc->set_config = vpnet_set_config;
    vdc->get_features = vpnet_get_features;
    vdc->set_features = vpnet_set_features;
    vdc->set_status = vpnet_set_status;
    vdc->reset = vpnet_reset;
}

static const TypeInfo vpnet_info = {
    .name = TYPE_VHOST_PCI_NET,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VhostPCINet),
    .instance_init = vpnet_instance_init,
    .class_init = vpnet_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vpnet_info);
}

type_init(virtio_register_types)
