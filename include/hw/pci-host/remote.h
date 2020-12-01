/*
 * PCI Host for remote device
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_PCIHOST_H
#define REMOTE_PCIHOST_H

#include "exec/memory.h"
#include "hw/pci/pcie_host.h"

#define TYPE_REMOTE_HOST_DEVICE "remote-pcihost"
#define REMOTE_HOST_DEVICE(obj) \
    OBJECT_CHECK(RemotePCIHost, (obj), TYPE_REMOTE_HOST_DEVICE)

typedef struct RemotePCIHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/

    MemoryRegion *mr_pci_mem;
    MemoryRegion *mr_sys_io;
} RemotePCIHost;

#endif
