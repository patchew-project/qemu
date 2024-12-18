/*
 * PLX PEX PCIe Virtual Switch - Downstream
 *
 * Copyright 2024 Google LLC
 * Author: Nabih Estefan <nabihestefan@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * Based on xio3130_downstream.c and guest_only_pci.c
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci-bridge/plx_vswitch.h"
#include "hw/qdev-properties-system.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TYPE_PLX_VSWITCH_DOWNSTREAM_PCI "plx-vswitch-downstream-pci"
OBJECT_DECLARE_SIMPLE_TYPE(PlxVSwitchPci, PLX_VSWITCH_DOWNSTREAM_PCI)


static void plx_vswitch_downstream_write_config(PCIDevice *d, uint32_t address,
                                         uint32_t val, int len)
{
    pci_bridge_write_config(d, address, val, len);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
}

static void plx_vswitch_downstream_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);

    pcie_cap_deverr_reset(d);
    pcie_cap_arifwd_reset(d);
    pci_bridge_reset(qdev);
}

static void plx_vswitch_downstream_realize(PCIDevice *d, Error **errp)
{
    PlxVSwitchPci *vs = PLX_VSWITCH_DOWNSTREAM_PCI(d);
    PCIEPort *p = PCIE_PORT(d);
    int rc;

    if (vs->vendor_id == 0xffff) {
        error_setg(errp, "Vendor ID invalid, it must always be supplied");
        return;
    }
    if (vs->device_id == 0xffff) {
        error_setg(errp, "Device ID invalid, it must always be supplied");
        return;
    }

    if (vs->subsystem_vendor_id == 0xffff) {
        error_setg(errp,
                   "Subsystem Vendor ID invalid, it must always be supplied");
        return;
    }

    uint16_t ssvid = vs->subsystem_vendor_id;
    uint16_t ssdid = vs->subsystem_device_id;

    pci_set_word(&d->config[PCI_VENDOR_ID], vs->vendor_id);
    pci_set_word(&d->config[PCI_DEVICE_ID], vs->device_id);
    pci_set_long(&d->config[PCI_CLASS_REVISION], vs->class_revision);

    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = msi_init(d, PLX_VSWITCH_MSI_OFFSET, PLX_VSWITCH_MSI_NR_VECTOR,
                  PLX_VSWITCH_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  PLX_VSWITCH_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT,
                  errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
        goto err_bridge;
    }

    rc = pci_bridge_ssvid_init(d, PLX_VSWITCH_SSVID_OFFSET, ssvid, ssdid,
                               errp);
    if (rc < 0) {
        goto err_msi;
    }

    rc = pcie_cap_init(d, PLX_VSWITCH_EXP_OFFSET, PCI_EXP_TYPE_DOWNSTREAM,
                       p->port, errp);
    if (rc < 0) {
        goto err_msi;
    }
    pcie_cap_flr_init(d);
    pcie_cap_deverr_init(d);
    pcie_cap_arifwd_init(d);

    rc = pcie_aer_init(d, PCI_ERR_VER, PLX_VSWITCH_AER_OFFSET,
                       PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err;
    }

    return;

err:
    pcie_cap_exit(d);
err_msi:
    msi_uninit(d);
err_bridge:
    pci_bridge_exitfn(d);
}

static void plx_vswitch_downstream_exitfn(PCIDevice *d)
{
    pcie_aer_exit(d);
    pcie_cap_exit(d);
    msi_uninit(d);
    pci_bridge_exitfn(d);
}

static const VMStateDescription vmstate_plx_vswitch_downstream = {
    .name = PLX_VSWITCH_DOWNSTREAM,
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj, PCIEPort),
        VMSTATE_STRUCT(parent_obj.parent_obj.exp.aer_log,
                       PCIEPort, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static Property plx_vswitch_downstream_pci_properties[] = {
    DEFINE_PROP_UINT16("vendor-id", PlxVSwitchPci, vendor_id, 0xffff),
    DEFINE_PROP_UINT16("device-id", PlxVSwitchPci, device_id, 0xffff),
    DEFINE_PROP_UINT16("subsystem-vendor-id", PlxVSwitchPci,
                       subsystem_vendor_id, 0),
    DEFINE_PROP_UINT16("subsystem-device-id", PlxVSwitchPci,
                       subsystem_device_id, 0),
    DEFINE_PROP_UINT32("class-revision", PlxVSwitchPci, class_revision,
                       0xff000000 /* Unknown class */),
    DEFINE_PROP_BIT(COMPAT_PROP_PCP, PCIDevice, cap_present,
                    QEMU_PCIE_SLTCAP_PCP_BITNR, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void plx_vswitch_downstream_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Downstream Port of PLX PEX PCIe Virtual Switch";
    device_class_set_legacy_reset(dc, plx_vswitch_downstream_reset);
    dc->vmsd = &vmstate_plx_vswitch_downstream;
    device_class_set_props(dc, plx_vswitch_downstream_pci_properties);

    k->config_write = plx_vswitch_downstream_write_config;
    k->realize = plx_vswitch_downstream_realize;
    k->exit = plx_vswitch_downstream_exitfn;
}

static const TypeInfo plx_vswitch_downstream_pci_types[] = {
    {
        .name = TYPE_PLX_VSWITCH_DOWNSTREAM_PCI,
        .parent = TYPE_PCIE_PORT,
        .class_init = plx_vswitch_downstream_class_init,
        .interfaces = (InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { }
        }
    },
};
DEFINE_TYPES(plx_vswitch_downstream_pci_types)
