/*
 * Virtio Network Device
 *
 * Copyright Intel, Corp. 2016
 *
 * Authors:
 *  Wei Wang   <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VHOST_PCI_NET_H
#define _QEMU_VHOST_PCI_NET_H

#include "standard-headers/linux/vhost_pci_net.h"
#include "hw/virtio/virtio.h"

#define TYPE_VHOST_PCI_NET "vhost-pci-net-device"
#define VHOST_PCI_NET(obj) \
        OBJECT_CHECK(VhostPCINet, (obj), TYPE_VHOST_PCI_NET)

typedef struct VhostPCINet {
    VirtIODevice parent_obj;
    /* Control receiveq: msg from host to guest */
    VirtQueue *crq;
    /* Datapath receiveq */
    VirtQueue **rqs;
    uint16_t status;
    uint16_t peer_vq_num;
    size_t config_size;
    uint64_t device_features;
} VhostPCINet;

#endif
