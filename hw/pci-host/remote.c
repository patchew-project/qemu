/*
 * Remote PCI host device
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_host.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/remote.h"
#include "exec/memory.h"

static const char *remote_pcihost_root_bus_path(PCIHostState *host_bridge,
                                                PCIBus *rootbus)
{
    return "0000:00";
}

static void remote_pcihost_realize(DeviceState *dev, Error **errp)
{
    char *busname = g_strdup_printf("remote-pci-%ld", (unsigned long)getpid());
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    RemotePCIHost *s = REMOTE_HOST_DEVICE(dev);

    pci->bus = pci_root_bus_new(DEVICE(s), busname,
                                s->mr_pci_mem, s->mr_sys_io,
                                0, TYPE_PCIE_BUS);
}

static void remote_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = remote_pcihost_root_bus_path;
    dc->realize = remote_pcihost_realize;

    dc->user_creatable = false;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
}

static const TypeInfo remote_pcihost_info = {
    .name = TYPE_REMOTE_HOST_DEVICE,
    .parent = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(RemotePCIHost),
    .class_init = remote_pcihost_class_init,
};

static void remote_pcihost_register(void)
{
    type_register_static(&remote_pcihost_info);
}

type_init(remote_pcihost_register)
