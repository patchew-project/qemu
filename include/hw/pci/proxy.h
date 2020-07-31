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

#define TYPE_PCI_PROXY_DEV "pci-proxy-dev"

#define PCI_PROXY_DEV(obj) \
            OBJECT_CHECK(PCIProxyDev, (obj), TYPE_PCI_PROXY_DEV)

typedef struct PCIProxyDev {
    PCIDevice parent_dev;
    char *fd;
    QIOChannel *ioc;
} PCIProxyDev;

#endif /* PROXY_H */
