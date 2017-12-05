/*
 * vhost-pci-net support
 *
 * Copyright Intel, Inc. 2017
 *
 * Authors:
 *  Wei Wang <wei.w.wang@intel.com>
 *  Zhiyong Yang <zhiyong.yang@intel.com>
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
#include "hw/pci/pci.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-pci-net.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/vhost-pci-slave.h"

static uint64_t vpnet_get_features(VirtIODevice *vdev, uint64_t features,
                                   Error **errp)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    features |= vpnet->host_features;

    return features;
}

static void vpnet_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    struct vpnet_config netcfg;

    virtio_stw_p(vdev, &netcfg.status, vpnet->status);
    memcpy(config, &netcfg, vpnet->config_size);
}

static void vpnet_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);

    qemu_chr_fe_set_handlers(&vpnet->chr_be, vp_slave_can_read,
                             vp_slave_read, vp_slave_event, NULL,
                             vpnet, NULL, true);

    virtio_init(vdev, "vhost-pci-net", VIRTIO_ID_VHOST_PCI_NET,
                vpnet->config_size);

    memory_region_init_ram(&vpnet->metadata_region, NULL,
                           "Metadata", METADATA_SIZE, NULL);
    memory_region_add_subregion(&vpnet->bar_region, 0,
                                &vpnet->metadata_region);
    vpnet->metadata = memory_region_get_ram_ptr(&vpnet->metadata_region);
    memset(vpnet->metadata, 0, METADATA_SIZE);
}

static void vpnet_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostPCINet *vpnet = VHOST_PCI_NET(vdev);
    int i, ret, nregions = vpnet->metadata->nregions;

    for (i = 0; i < nregions; i++) {
        ret = munmap(vpnet->remote_mem_base[i], vpnet->remote_mem_map_size[i]);
        if (ret < 0) {
            error_report("%s: failed to unmap mr[%d]", __func__, i);
            continue;
        }
        memory_region_del_subregion(&vpnet->bar_region,
                                    &vpnet->remote_mem_region[i]);
    }

    qemu_chr_fe_deinit(&vpnet->chr_be, true);
    virtio_cleanup(vdev);
}

static Property vpnet_properties[] = {
    DEFINE_PROP_BIT("mrg_rxbuf", VhostPCINet, host_features,
                    VIRTIO_NET_F_MRG_RXBUF, true),
    DEFINE_PROP_CHR("chardev", VhostPCINet, chr_be),
    DEFINE_PROP_END_OF_LIST(),
};

static void vpnet_instance_init(Object *obj)
{
    VhostPCINet *vpnet = VHOST_PCI_NET(obj);

    vpnet->config_size = sizeof(struct vpnet_config);
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
    vdc->get_features = vpnet_get_features;
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
