/*
 * Copyright 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_PROXY_H
#define QEMU_PROXY_H

#include "io/proxy-link.h"

#define TYPE_PCI_PROXY_DEV "pci-proxy-dev"
#define PCI_PROXY_DEV(obj) \
            OBJECT_CHECK(PCIProxyDev, (obj), TYPE_PCI_PROXY_DEV)

typedef struct proxy_device {
       int n_mr_sections;
        MemoryRegionSection *mr_sections;
        ProxyLinkState *proxy_link;
        int link_up;
} proxy_device;

typedef struct PCIProxyDev {
        PCIDevice parent_dev;
        struct proxy_device proxy_dev;

        MemoryRegion bar;
        MemoryRegion mmio_io;
        MemoryRegion ram_io;

        void (*proxy_read_config)(void);
        void (*proxy_write_config) (void);

} PCIProxyDev;

void init_emulation_process(PCIProxyDev *pdev, char *command, Error **errp);
int config_op_send(PCIProxyDev *dev, uint32_t addr, uint32_t val, int l,
                                                       unsigned int op);

#endif
