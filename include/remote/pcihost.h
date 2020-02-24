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

#include <stddef.h>
#include <stdint.h>

#include "exec/memory.h"
#include "hw/pci/pcie_host.h"

#define TYPE_REMOTE_HOST_DEVICE "remote-pcihost"
#define REMOTE_HOST_DEVICE(obj) \
    OBJECT_CHECK(RemPCIHost, (obj), TYPE_REMOTE_HOST_DEVICE)

typedef struct RemPCIHost {
    /*< private >*/
    PCIExpressHost parent_obj;
    /*< public >*/

    /*
     * Memory Controller Hub (MCH) may not be necessary for the emulation
     * program. The two important reasons for implementing a PCI host in the
     * emulation program are:
     * - Provide a PCI bus for IO devices
     * - Enable translation of guest PA to the PCI bar regions
     *
     * For both the above mentioned purposes, it doesn't look like we would
     * need the MCH
     */

    MemoryRegion *mr_pci_mem;
    MemoryRegion *mr_sys_mem;
    MemoryRegion *mr_sys_io;
} RemPCIHost;

#endif
