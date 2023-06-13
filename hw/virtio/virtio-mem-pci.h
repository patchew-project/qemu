/*
 * Virtio MEM PCI device
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_VIRTIO_MEM_PCI_H
#define QEMU_VIRTIO_MEM_PCI_H

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-mem.h"
#include "qom/object.h"

typedef struct VirtIOMEMPCI VirtIOMEMPCI;

/*
 * virtio-mem-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_MEM_PCI "virtio-mem-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOMEMPCI, VIRTIO_MEM_PCI,
                         TYPE_VIRTIO_MEM_PCI)

struct VirtIOMEMPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOMEM vdev;
    Notifier size_change_notifier;
};

void virtio_mem_pci_unplug_request_check(VirtIOMEMPCI *pci_mem, Error **errp);

#endif /* QEMU_VIRTIO_MEM_PCI_H */
