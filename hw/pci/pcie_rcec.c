/*
 * pcie_rcec.c
 * PCIe Root Complex Event Collector emulation
 *
 * Copyright (c) 2021 Mayuresh Chitale <mchitale@ventanamicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_RCEC_DEVICE "pcie-rcec"
#define PCIE_RCEC_EXP_CAP_OFF 0x40
#define PCIE_RCEC_EP_ECAP_OFF 0x100
#define PCIE_RCEC_AER_ECAP_OFF 0x120

struct RcecState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/
};


static int pcie_rcec_cap_init(PCIDevice *dev, uint8_t offset)
{
    int rc;

    dev->config[PCI_INTERRUPT_PIN] = 1;
    rc = pcie_endpoint_cap_common_init(dev, offset,
            PCI_EXP_VER2_SIZEOF, PCI_EXP_TYPE_RC_EC);
    pcie_cap_root_init(dev);
    pcie_cap_deverr_init(dev);

    return rc;
}

static void pcie_rcec_ep_cap_init(PCIDevice *dev, uint8_t cap_ver, uint16_t offset,
                  uint16_t size, Error **errp)
{
    pcie_add_capability(dev, PCI_EXT_CAP_ID_RCEC, cap_ver, offset, size);
    /* Map device (bit) 1 which is RCEC by default */
    pci_set_long(dev->config + offset + 0x4, 0x2);
}

static void pcie_rcec_realize(PCIDevice *pci_dev, Error **errp)
{
    if (pcie_rcec_cap_init(pci_dev, PCIE_RCEC_EXP_CAP_OFF) < 0)
        hw_error("Failed to initialize RCEC express capability");

    pcie_rcec_ep_cap_init(pci_dev, PCI_RCEC_EP_VER, PCIE_RCEC_EP_ECAP_OFF,
        PCI_RCEC_EP_SIZEOF, errp);

    if (pcie_aer_init(pci_dev, PCI_ERR_VER, PCIE_RCEC_AER_ECAP_OFF,
        PCI_ERR_SIZEOF, errp) < 0)
        hw_error("Failed to initialize RCEC AER capability");
}

static const VMStateDescription vmstate_rcec = {
    .name = "rcec",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, struct RcecState),
        VMSTATE_END_OF_LIST()
    }
};

static void rcec_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "QEMU generic RCEC";
    dc->vmsd = &vmstate_rcec;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_RCEC;
    k->revision = 0;
    k->class_id = PCI_CLASS_SYSTEM_RCEC;
    k->realize = pcie_rcec_realize;
}

static const TypeInfo pcie_rcec_info = {
    .name = TYPE_RCEC_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct RcecState),
    .class_init = rcec_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};


static void pcie_rcec_register_types(void)
{
    type_register_static(&pcie_rcec_info);
}

type_init(pcie_rcec_register_types)
