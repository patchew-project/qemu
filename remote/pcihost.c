/*
 * Remote PCI host device
 *
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

#include <sys/types.h>

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_host.h"
#include "remote/pcihost.h"
#include "exec/memory.h"

static const char *remote_host_root_bus_path(PCIHostState *host_bridge,
                                             PCIBus *rootbus)
{
    return "0000:00";
}

static void remote_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    RemPCIHost *s = REMOTE_HOST_DEVICE(dev);

    /*
     * TODO: the name of the bus would be provided by QEMU. Use
     * "pcie.0" for now.
     */
    pci->bus = pci_root_bus_new(DEVICE(s), "pcie.0",
                                s->mr_pci_mem, s->mr_sys_io,
                                0, TYPE_PCIE_BUS);
}

static Property remote_host_props[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void remote_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = remote_host_root_bus_path;
    dc->realize = remote_host_realize;
    dc->props = remote_host_props;

    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
}

static const TypeInfo remote_host_info = {
    .name = TYPE_REMOTE_HOST_DEVICE,
    .parent = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(RemPCIHost),
    .class_init = remote_host_class_init,
};

static void remote_machine_register(void)
{
    type_register_static(&remote_host_info);
}

type_init(remote_machine_register)
