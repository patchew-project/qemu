/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PROXY_H
#define QEMU_PROXY_H

#include <linux/kvm.h>

#include "io/mpqemu-link.h"
#include "hw/proxy/memory-sync.h"
#include "qemu/event_notifier.h"
#include "hw/pci/pci.h"
#include "block/qdict.h"

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

extern const MemoryRegionOps proxy_default_ops;

struct PCIProxyDev {
    PCIDevice parent_dev;

    int n_mr_sections;
    MemoryRegionSection *mr_sections;

    MPQemuLinkState *mpqemu_link;

    RemoteMemSync *sync;
    bool mem_init;
    struct kvm_irqfd irqfd;

    EventNotifier intr;
    EventNotifier resample;

    pid_t remote_pid;
    EventNotifier en_ping;

    int socket;
    int mmio_sock;

    char *rid;
    char *dev_id;
    bool managed;
    QLIST_ENTRY(PCIProxyDev) next;

    void (*set_proxy_sock) (PCIDevice *dev, int socket);
    int (*get_proxy_sock) (PCIDevice *dev);

    int (*set_remote_opts) (PCIDevice *dev, QDict *qdict, unsigned int cmd);
    void (*proxy_ready) (PCIDevice *dev);
    void (*init_proxy) (PCIDevice *dev, char *command, char *exec_name,
                        bool need_spawn, Error **errp);

    ProxyMemoryRegion region[PCI_NUM_REGIONS];

    VMChangeStateEntry *vmcse;

    uint64_t migsize;
};

typedef struct PCIProxyDevClass {
    PCIDeviceClass parent_class;

    void (*realize)(PCIProxyDev *dev, Error **errp);

    char *command;
} PCIProxyDevClass;

typedef struct PCIProxyDevList {
    QLIST_HEAD(, PCIProxyDev) devices;
} proxy_dev_list_t;

extern QemuMutex proxy_list_lock;
extern proxy_dev_list_t proxy_dev_list;

void proxy_default_bar_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size);

uint64_t proxy_default_bar_read(void *opaque, hwaddr addr, unsigned size);

#endif /* QEMU_PROXY_H */
