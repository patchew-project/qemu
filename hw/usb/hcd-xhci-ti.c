/*
 * USB xHCI TI TUSB73X0 controller emulation
 * Datasheet https://www.ti.com/product/TUSB7340
 *
 * Copyright (c) 2025 IBM Corporation
 * Derived from hcd-xhci-nec.c, copyright accordingly.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "hw/usb.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"

#include "hcd-xhci-pci.h"

OBJECT_DECLARE_SIMPLE_TYPE(XHCITiState, TI_XHCI)

struct XHCITiState {
    XHCIPciState parent_obj;

    uint32_t intrs;
    uint32_t slots;
};

static const Property ti_xhci_properties[] = {
    DEFINE_PROP_UINT32("intrs", XHCITiState, intrs, 8),
    DEFINE_PROP_UINT32("slots", XHCITiState, slots, XHCI_MAXSLOTS),
};

static void ti_xhci_instance_init(Object *obj)
{
    XHCIPciState *pci = XHCI_PCI(obj);
    XHCITiState *ti = TI_XHCI(obj);

    pci->xhci.numintrs = ti->intrs;
    pci->xhci.numslots = ti->slots;

    /* Taken from datasheet */
    pci->cache_line_size = 0x0;
    pci->pm_cap_off = 0x40;
    pci->pcie_cap_off = 0x70;
    pci->msi_cap_off = 0x48;
    pci->msix_cap_off = 0xc0;
    pci->msix_bar_nr = 0x2;
    pci->msix_bar_size = 0x800000;
    pci->msix_table_off = 0x0;
    pci->msix_pba_off = 0x1000;
}

static void ti_xhci_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, ti_xhci_properties);
    k->vendor_id    = PCI_VENDOR_ID_TI;
    k->device_id    = PCI_DEVICE_ID_TI_TUSB73X0;
    k->revision     = 0x02;
}

static const TypeInfo ti_xhci_info = {
    .name          = TYPE_TI_XHCI,
    .parent        = TYPE_XHCI_PCI,
    .instance_size = sizeof(XHCITiState),
    .instance_init = ti_xhci_instance_init,
    .class_init    = ti_xhci_class_init,
};

static void ti_xhci_register_types(void)
{
    type_register_static(&ti_xhci_info);
}

type_init(ti_xhci_register_types)
