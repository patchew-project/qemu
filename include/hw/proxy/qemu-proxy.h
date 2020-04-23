/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PROXY_H
#define QEMU_PROXY_H

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "io/mpqemu-link.h"
#include "hw/pci/pci.h"

#define TYPE_PCI_PROXY_DEV "pci-proxy-dev"

#define PCI_PROXY_DEV(obj) \
            OBJECT_CHECK(PCIProxyDev, (obj), TYPE_PCI_PROXY_DEV)

#define PCI_PROXY_DEV_CLASS(klass) \
            OBJECT_CLASS_CHECK(PCIProxyDevClass, (klass), TYPE_PCI_PROXY_DEV)

#define PCI_PROXY_DEV_GET_CLASS(obj) \
            OBJECT_GET_CLASS(PCIProxyDevClass, (obj), TYPE_PCI_PROXY_DEV)

typedef struct PCIProxyDev PCIProxyDev;

typedef struct ProxyMemoryRegion {
    PCIProxyDev *dev;
    MemoryRegion mr;
    bool memory;
    bool present;
    uint8_t type;
} ProxyMemoryRegion;

struct PCIProxyDev {
    PCIDevice parent_dev;

    MPQemuLinkState *mpqemu_link;

    int socket;

    ProxyMemoryRegion region[PCI_NUM_REGIONS];
};

typedef struct PCIProxyDevClass {
    PCIDeviceClass parent_class;

    void (*realize)(PCIProxyDev *dev, Error **errp);

    char *command;
} PCIProxyDevClass;

void proxy_default_bar_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size);

uint64_t proxy_default_bar_read(void *opaque, hwaddr addr, unsigned size);

#endif /* QEMU_PROXY_H */
