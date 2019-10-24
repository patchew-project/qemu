/*
 * Copyright 2019, Oracle and/or its affiliates.
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

#include <sys/types.h>

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qemu/bitops.h"
#include "hw/pci/pci.h"
#include "hw/proxy/qemu-proxy.h"
#include "hw/proxy/proxy-lsi53c895a.h"
#include "exec/memory.h"

static void proxy_lsi_realize(PCIProxyDev *dev, Error **errp)
{
    ProxyLSIState *s = LSI_PROXY_DEV(dev);
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pci_dev->config;

    pci_conf[PCI_LATENCY_TIMER] = 0xff;
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    dev->region[0].present = true;
    dev->region[0].type = PCI_BASE_ADDRESS_SPACE_IO;
    memory_region_init_io(&dev->region[0].mr, OBJECT(s), &proxy_default_ops,
                          &dev->region[0], "proxy-lsi-io", 256);

    dev->region[1].present = true;
    dev->region[1].memory = true;
    dev->region[1].type = PCI_BASE_ADDRESS_SPACE_MEMORY;
    memory_region_init_io(&dev->region[1].mr, OBJECT(s), &proxy_default_ops,
                          &dev->region[1], "proxy-lsi-mmio", 0x400);

    dev->region[2].present = true;
    dev->region[2].memory = true;
    dev->region[2].type = PCI_BASE_ADDRESS_SPACE_MEMORY;
    memory_region_init_io(&dev->region[2].mr, OBJECT(s), &proxy_default_ops,
                          &dev->region[2], "proxy-lsi-ram", 0x2000);
}

static void proxy_lsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pci_class = PCI_DEVICE_CLASS(klass);
    PCIProxyDevClass *proxy_class = PCI_PROXY_DEV_CLASS(klass);

    proxy_class->realize = proxy_lsi_realize;
    proxy_class->command = g_strdup("qemu-scsi-dev");

    pci_class->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pci_class->device_id = PCI_DEVICE_ID_LSI_53C895A;
    pci_class->class_id = PCI_CLASS_STORAGE_SCSI;
    pci_class->subsystem_id = 0x1000;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->desc = "LSI Proxy Device";
}

static const TypeInfo lsi_proxy_dev_type_info = {
    .name          = TYPE_PROXY_LSI53C895A,
    .parent        = TYPE_PCI_PROXY_DEV,
    .instance_size = sizeof(ProxyLSIState),
    .class_init    = proxy_lsi_class_init,
};

static void lsi_proxy_dev_register_types(void)
{
    type_register_static(&lsi_proxy_dev_type_info);
}

type_init(lsi_proxy_dev_register_types)
