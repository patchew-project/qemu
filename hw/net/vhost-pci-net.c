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
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-pci-net.h"

#define VPNET_CQ_SIZE 32
#define VPNET_RQ_SIZE 256

void vpnet_set_peer_vq_num(VhostPCINet *vpnet, uint16_t num)
{
    vpnet->peer_vq_num = num;
}

void vpnet_init_device_features(VhostPCINet *vpnet, uint64_t features)
{
    vpnet->device_features = features;
}

void vpnet_set_peer_vq_msg(VhostPCINet *vpnet, PeerVqNode *vq_node)
{
    struct peer_vq_msg *pvq_msg;
    uint32_t vring_num = vq_node->vring_num;

    if (vpnet->pvq_msg == NULL)
        vpnet->pvq_msg = g_malloc0(sizeof(struct peer_vq_msg) * (vring_num + 1));

    pvq_msg = vpnet->pvq_msg + vring_num;
    pvq_msg->last_avail_idx = vq_node->last_avail_idx;
    pvq_msg->vring_num = vring_num;
    pvq_msg->vring_enable = vq_node->enabled;
    pvq_msg->desc_gpa = vq_node->addr.desc_user_addr;
    pvq_msg->avail_gpa = vq_node->addr.avail_user_addr;
    pvq_msg->used_gpa = vq_node->addr.used_user_addr;
}

static void vpnet_handle_rq(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vpnet_handle_crq(VirtIODevice *vdev, VirtQueue *vq)
{
}

static size_t vpnet_send_crq_msg(VhostPCINet *vpnet,
                                 struct vpnet_controlq_msg *msg)
{
    VirtQueueElement *elem;
    VirtQueue *vq;
    size_t msg_len = VPNET_CQ_MSG_HDR_SIZE + msg->size;

    vq = vpnet->crq;
    if (!virtio_queue_ready(vq)) {
        return 0;
    }

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        return 0;
    }

    /* TODO: detect a buffer that's too short, set NEEDS_RESET */
    iov_from_buf(elem->in_sg, elem->in_num, 0, msg, msg_len);

    virtqueue_push(vq, elem, msg_len);
    virtio_notify(VIRTIO_DEVICE(vpnet), vq);
    g_free(elem);

    return msg_len;
}

static void vpnet_send_peer_mem_msg(VhostPCINet *vpnet)
{
    struct vpnet_controlq_msg msg = {
        .class = VHOST_PCI_CTRL_PEER_MEM_MSG,
        .size = sizeof(struct peer_mem_msg),
    };
    memcpy(&msg.payload.pmem_msg, &vp_slave->pmem_msg, msg.size);
    vpnet_send_crq_msg(vpnet, &msg);
}

static void vpnet_send_peer_vq_msg(VhostPCINet *vpnet)
{
    struct vpnet_controlq_msg *msg;
    struct peer_vqs_msg *pvqs_msg;
    struct peer_vq_msg *pvq_msg;
    uint16_t pvq_num, msg_size, payload_size;

    pvq_num = vpnet->peer_vq_num;
    payload_size = sizeof(struct peer_vqs_msg)
                   + sizeof(struct peer_vq_msg) * pvq_num;
    msg_size = VPNET_CQ_MSG_HDR_SIZE + payload_size;
    msg = g_malloc(msg_size);
    msg->class = VHOST_PCI_CTRL_PEER_VQ_MSG,
    msg->size = msg_size;

    pvqs_msg = &msg->payload.pvqs_msg;
    pvqs_msg->nvqs = pvq_num;
    pvq_msg = pvqs_msg->pvq_msg;
    memcpy(pvq_msg, vpnet->pvq_msg, payload_size);

    vpnet_send_crq_msg(vpnet, msg);
    g_free(msg);
}

static void vpnet_set_status(struct VirtIODevice *vdev, uint8_t status)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);

    if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
        vpnet_send_peer_mem_msg(vpnet);
        vpnet_send_peer_vq_msg(vpnet);
    }
}

static uint64_t vpnet_get_features(VirtIODevice *vdev, uint64_t features, Error **errp)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);

    return vpnet->device_features;
}

static void vpnet_set_features(VirtIODevice *vdev, uint64_t features)
{
}

static void vpnet_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    struct vhost_pci_net_config netcfg;

    virtio_stw_p(vdev, &netcfg.status, vpnet->status);
    virtio_stw_p(vdev, &netcfg.peer_vq_num, vpnet->peer_vq_num);
    memcpy(config, &netcfg, vpnet->config_size);
}

static void vpnet_set_config(VirtIODevice *vdev, const uint8_t *config)
{
}

static void vpnet_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    uint16_t i, rq_num = vpnet->peer_vq_num / 2;

    virtio_init(vdev, "vhost-pci-net", VIRTIO_ID_VHOST_PCI_NET,
                vpnet->config_size);

    /* control receive quque: host to guest */
    vpnet->crq = virtio_add_queue(vdev, VPNET_CQ_SIZE, vpnet_handle_crq);
    /* datapath receive queue */
    vpnet->rqs = g_malloc0(sizeof(VirtQueue *) * rq_num);
    for (i = 0; i < rq_num; i++) {
        vpnet->rqs[i] = virtio_add_queue(vdev,VPNET_RQ_SIZE,
                                         vpnet_handle_rq);
    }
    vpnet->status = 0;
}

static void vpnet_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    uint16_t i, rq_num = vpnet->peer_vq_num / 2;

    for (i = 0; i < rq_num + 2; i++)
        virtio_del_queue(vdev, i);

    g_free(vpnet->rqs);
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
