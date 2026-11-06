/*
 * CXL Type-2 device
 *
 * Copyright(C) 2024 NVIDIA Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/cxl/cxl.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "system/hostmem.h"

static void clean_memory(PCIDevice *pci_dev)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);

    address_space_destroy(&ct2d->hostvmem_as);
    host_memory_backend_set_mapped(ct2d->hostvmem, false);
}

static bool setup_memory(PCIDevice *pci_dev, Error **errp)
{
    CXLType2Dev *ct2d = CXL_TYPE2(pci_dev);
    MemoryRegion *vmr;

    if (!ct2d->hostvmem) {
        error_setg(errp, "volatile-memdev property must be set");
        return false;
    }
    vmr = host_memory_backend_get_memory(ct2d->hostvmem);
    if (!vmr) {
        error_setg(errp, "volatile memdev must have backing device");
        return false;
    }
    if (host_memory_backend_is_mapped(ct2d->hostvmem)) {
        error_setg(errp, "memory backend %s can't be used multiple times.",
                   object_get_canonical_path_component(OBJECT(ct2d->hostvmem)));
        return false;
    }
    memory_region_set_nonvolatile(vmr, false);
    memory_region_set_enabled(vmr, true);
    host_memory_backend_set_mapped(ct2d->hostvmem, true);
    address_space_init(&ct2d->hostvmem_as, vmr, "cxl-type2-dpa-vmem-space");
    return true;
}

static void cxl_type2_realize(PCIDevice *pci_dev, Error **errp)
{
    if (!setup_memory(pci_dev, errp)) {
        return;
    }

    pcie_endpoint_cap_init(pci_dev, 0x80);
}

static void cxl_type2_exit(PCIDevice *pci_dev)
{
    clean_memory(pci_dev);
}

static const Property cxl_type2_props[] = {
    DEFINE_PROP_LINK("volatile-memdev", CXLType2Dev, hostvmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
};

static const VMStateDescription cxl_type2_vmsd = {
    .name = TYPE_CXL_TYPE2,
    .unmigratable = 1,
};

static void cxl_type2_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = cxl_type2_realize;
    pc->exit = cxl_type2_exit;

    pc->class_id = PCI_CLASS_ACCELERATOR_PROCESSING;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_CXL_TYPE2;
    pc->revision = 1;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "CXL Type-2 device";
    dc->vmsd = &cxl_type2_vmsd;
    device_class_set_props(dc, cxl_type2_props);
}

static const TypeInfo cxl_type2_dev_info = {
    .name = TYPE_CXL_TYPE2,
    .parent = TYPE_PCI_DEVICE,
    .class_init = cxl_type2_class_init,
    .instance_size = sizeof(CXLType2Dev),
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CXL_DEVICE },
        { INTERFACE_PCIE_DEVICE },
        {}
    },
};

static void cxl_type2_dev_registers(void)
{
    type_register_static(&cxl_type2_dev_info);
}

type_init(cxl_type2_dev_registers);
