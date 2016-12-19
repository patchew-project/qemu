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
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-pci-net.h"

#define VPNET_CQ_SIZE 32
#define VPNET_RQ_SIZE 256

static void vpnet_handle_rq(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vpnet_handle_crq(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vpnet_set_status(struct VirtIODevice *vdev, uint8_t status)
{
}

static uint64_t vpnet_get_features(VirtIODevice *vdev, uint64_t features,
                                   Error **errp)
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
        vpnet->rqs[i] = virtio_add_queue(vdev, VPNET_RQ_SIZE,
                                         vpnet_handle_rq);
    }
    vpnet->status = 0;
}

static void vpnet_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    uint16_t i, rq_num = vpnet->peer_vq_num / 2;

    for (i = 0; i < rq_num + 2; i++) {
        virtio_del_queue(vdev, i);
    }

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
