/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PROXY_H
#define QEMU_PROXY_H

#include "io/mpqemu-link.h"

#define TYPE_PCI_PROXY_DEV "pci-proxy-dev"

#define PCI_PROXY_DEV(obj) \
            OBJECT_CHECK(PCIProxyDev, (obj), TYPE_PCI_PROXY_DEV)

#define PCI_PROXY_DEV_CLASS(klass) \
            OBJECT_CLASS_CHECK(PCIProxyDevClass, (klass), TYPE_PCI_PROXY_DEV)

#define PCI_PROXY_DEV_GET_CLASS(obj) \
            OBJECT_GET_CLASS(PCIProxyDevClass, (obj), TYPE_PCI_PROXY_DEV)

typedef struct PCIProxyDev {
    PCIDevice parent_dev;

    MPQemuLinkState *mpqemu_link;

    pid_t remote_pid;
    int socket;

    char *rid;

    bool managed;

    void (*set_proxy_sock) (PCIDevice *dev, int socket);
    int (*get_proxy_sock) (PCIDevice *dev);

    void (*proxy_ready) (PCIDevice *dev);
    void (*init_proxy) (PCIDevice *dev, char *command, char *exec_name,
                        Error **errp);

} PCIProxyDev;

typedef struct PCIProxyDevClass {
    PCIDeviceClass parent_class;

    void (*realize)(PCIProxyDev *dev, Error **errp);

    char *command;
} PCIProxyDevClass;

#endif /* QEMU_PROXY_H */
