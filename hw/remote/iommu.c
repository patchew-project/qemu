/*
 * Remote IOMMU
 *
 * Copyright © 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/iommu.h"
#include "hw/pci/pci_bus.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "trace.h"

struct VFUIOMMU {
    AddressSpace  as;
    MemoryRegion  mr;
};

typedef struct VFUPciBus {
    PCIBus           *bus;
    struct VFUIOMMU  *iommu[];
} VFUPciBus;

GHashTable *remote_as_table;

static AddressSpace *remote_iommu_get_as(PCIBus *bus, void *opaque, int devfn)
{
    VFUPciBus *vfu_pci_bus = NULL;
    struct VFUIOMMU *iommu = NULL;

    if (!remote_as_table) {
        return &address_space_memory;
    }

    vfu_pci_bus = g_hash_table_lookup(remote_as_table, bus);

    if (!vfu_pci_bus) {
        vfu_pci_bus = g_malloc0(sizeof(VFUPciBus));
        vfu_pci_bus->bus = bus;
        g_hash_table_insert(remote_as_table, bus, vfu_pci_bus);
    }

    iommu = vfu_pci_bus->iommu[devfn];

    if (!iommu) {
        g_autofree char *mr_name = g_strdup_printf("vfu-ram-%d", devfn);
        g_autofree char *as_name = g_strdup_printf("vfu-as-%d", devfn);

        iommu = g_malloc0(sizeof(struct VFUIOMMU));

        memory_region_init(&iommu->mr, NULL, mr_name, UINT64_MAX);
        address_space_init(&iommu->as, &iommu->mr, as_name);

        vfu_pci_bus->iommu[devfn] = iommu;
    }

    return &iommu->as;
}

void remote_iommu_free(PCIDevice *pci_dev)
{
    VFUPciBus *vfu_pci_bus = NULL;
    struct VFUIOMMU *iommu = NULL;

    if (!remote_as_table) {
        return;
    }

    vfu_pci_bus = g_hash_table_lookup(remote_as_table, pci_get_bus(pci_dev));

    if (!vfu_pci_bus) {
        return;
    }

    iommu = vfu_pci_bus->iommu[pci_dev->devfn];

    vfu_pci_bus->iommu[pci_dev->devfn] = NULL;

    if (iommu) {
        memory_region_unref(&iommu->mr);
        address_space_destroy(&iommu->as);
        g_free(iommu);
    }
}

void remote_iommu_init(void)
{
    remote_as_table = g_hash_table_new_full(NULL, NULL, NULL, NULL);
}

void remote_iommu_set(PCIBus *bus)
{
    pci_setup_iommu(bus, remote_iommu_get_as, NULL);
}

MemoryRegion *remote_iommu_get_ram(PCIDevice *pci_dev)
{
    PCIBus *bus = pci_get_bus(pci_dev);
    VFUPciBus *vfu_pci_bus;

    if (!remote_as_table) {
        return get_system_memory();
    }

    vfu_pci_bus = g_hash_table_lookup(remote_as_table, bus);
    if (!vfu_pci_bus) {
        return get_system_memory();
    }

    return &vfu_pci_bus->iommu[pci_dev->devfn]->mr;
}
