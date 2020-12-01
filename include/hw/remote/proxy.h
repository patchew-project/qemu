/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PROXY_H
#define PROXY_H

#include "hw/pci/pci.h"
#include "io/channel.h"
#include "hw/remote/memory-sync.h"
#include "qemu/event_notifier.h"

#define TYPE_PCI_PROXY_DEV "x-pci-proxy-dev"

#define PCI_PROXY_DEV(obj) \
            OBJECT_CHECK(PCIProxyDev, (obj), TYPE_PCI_PROXY_DEV)

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
    char *fd;

    /*
     * Mutex used to protect the QIOChannel fd from
     * the concurrent access by the VCPUs since proxy
     * blocks while awaiting for the replies from the
     * process remote.
     */
    QemuMutex io_mutex;
    QIOChannel *ioc;
    Error *migration_blocker;
    RemoteMemSync sync;
    int virq;
    EventNotifier intr;
    EventNotifier resample;

    ProxyMemoryRegion region[PCI_NUM_REGIONS];
};

#endif /* PROXY_H */
