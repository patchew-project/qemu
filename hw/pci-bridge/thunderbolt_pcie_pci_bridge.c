/*
 * Thunderbolt PCI Express to PCI Bridge
 *
 * This is the regular generic PCIe-to-PCI bridge used as a Thunderbolt PCIe
 * tunnel endpoint. Downstream PCI devices are described normally so existing
 * macOS drivers that are not IOPCITunnelCompatible can still bind to them.
 *
 * Copyright (C) 2026
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/pcihp.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "qom/object.h"

#define TYPE_THUNDERBOLT_PCIE_PCI_BRIDGE "thunderbolt-pcie-pci-bridge"

static Aml *build_device_properties_dsd(Aml *properties)
{
    Aml *dsd;

    dsd = aml_package(2);
    aml_append(dsd, aml_touuid("DAFFD814-6EBA-4D8C-8A91-BC9BBF4AA301"));
    aml_append(dsd, properties);

    return aml_name_decl("_DSD", dsd);
}

static void append_bool_property(Aml *properties, const char *name)
{
    Aml *property = aml_package(2);

    aml_append(property, aml_string("%s", name));
    aml_append(property, aml_int(1));
    aml_append(properties, property);
}

static void build_tunnelled_device_dsd(Aml *scope)
{
    Aml *properties = aml_package(1);

    append_bool_property(properties, "IOPCITunnelled");

    aml_append(scope, build_device_properties_dsd(properties));
}

static void build_tunnelled_device_acpi_properties(Aml *scope)
{
    build_tunnelled_device_dsd(scope);
}

static Aml *build_pci_static_endpoint_dsm(PCIDevice *pdev)
{
    Aml *method;
    Aml *params;
    Aml *pkg;

    g_assert(pdev->acpi_index != 0);

    method = aml_method("_DSM", 4, AML_SERIALIZED);
    params = aml_local(0);
    pkg = aml_package(1);
    aml_append(pkg, aml_int(pdev->acpi_index));
    aml_append(method, aml_store(pkg, params));
    aml_append(method,
        aml_return(aml_call5("EDSM", aml_arg(0), aml_arg(1), aml_arg(2),
                             aml_arg(3), params)));

    return method;
}

static bool thunderbolt_bridge_devfn_ignored(int devfn, const PCIBus *bus)
{
    const PCIDevice *pdev = bus->devices[devfn];

    if (PCI_FUNC(devfn) && IS_PCI_BRIDGE(pdev) && DEVICE(pdev)->hotplugged) {
        return true;
    }

    return false;
}

static void build_tunnelled_pci_bus_devices(Aml *parent_scope, PCIBus *bus)
{
    int devfn;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        PCIDevice *pdev = bus->devices[devfn];
        int adr = PCI_SLOT(devfn) << 16 | PCI_FUNC(devfn);
        Aml *dev;

        if (!pdev || thunderbolt_bridge_devfn_ignored(devfn, bus)) {
            continue;
        }

        dev = aml_device("S%.02X", devfn);
        aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));

        call_dev_aml_func(DEVICE(pdev), dev);

        if (pdev->acpi_index &&
            !object_property_get_bool(OBJECT(pdev), "hotpluggable",
                                      &error_abort)) {
            aml_append(dev, build_pci_static_endpoint_dsm(pdev));
        }

        aml_append(parent_scope, dev);
    }
}

static void build_thunderbolt_pcie_pci_bridge_aml(AcpiDevAmlIf *adev,
                                                  Aml *scope)
{
    PCIBridge *br = PCI_BRIDGE(adev);

    build_tunnelled_device_acpi_properties(scope);

    if (!DEVICE(br)->hotplugged) {
        PCIBus *sec_bus = pci_bridge_get_sec_bus(br);

        build_tunnelled_pci_bus_devices(scope, sec_bus);

        if (object_property_find(OBJECT(sec_bus), ACPI_PCIHP_PROP_BSEL)) {
            build_append_pcihp_slots(scope, sec_bus);
        }
    }
}

static void thunderbolt_pcie_pci_bridge_class_init(ObjectClass *klass,
                                                   const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->desc = "Thunderbolt PCI Express to PCI Bridge";
    adevc->build_dev_aml = build_thunderbolt_pcie_pci_bridge_aml;
}

static const TypeInfo thunderbolt_pcie_pci_bridge_info = {
    .name       = TYPE_THUNDERBOLT_PCIE_PCI_BRIDGE,
    .parent     = "pcie-pci-bridge",
    .class_init = thunderbolt_pcie_pci_bridge_class_init,
};

static void thunderbolt_pcie_pci_bridge_register_types(void)
{
    type_register_static(&thunderbolt_pcie_pci_bridge_info);
}

type_init(thunderbolt_pcie_pci_bridge_register_types)
