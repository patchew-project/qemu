/*
 * Virtio Network Device
 *
 * Copyright Intel, Corp. 2017
 *
 * Authors:
 *  Wei Wang <wei.w.wang@intel.com>
 *  Zhiyong Yang <zhiyong.yang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VHOST_PCI_NET_H
#define _QEMU_VHOST_PCI_NET_H

#include "standard-headers/linux/vhost_pci_net.h"
#include "hw/virtio/virtio.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_PCI_NET "vhost-pci-net-device"
#define VHOST_PCI_NET(obj) \
        OBJECT_CHECK(VhostPCINet, (obj), TYPE_VHOST_PCI_NET)

typedef struct VhostPCINet {
    VirtIODevice parent_obj;
    MemoryRegion bar_region;
    MemoryRegion metadata_region;
    MemoryRegion *remote_mem_region;
    struct vpnet_metadata *metadata;
    void *remote_mem_base[MAX_REMOTE_REGION];
    uint64_t remote_mem_map_size[MAX_REMOTE_REGION];
    uint32_t host_features;
    size_t config_size;
    uint16_t status;
    CharBackend chr_be;
} VhostPCINet;

#endif
