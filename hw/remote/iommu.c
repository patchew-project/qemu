/**
 * IOMMU for remote device
 *
 * Copyright © 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/iommu.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "trace.h"

struct RemoteIommuElem {
    AddressSpace  as;
    MemoryRegion  mr;
};

struct RemoteIommuTable {
    QemuMutex lock;
    GHashTable *elem_by_bdf;
} remote_iommu_table;

#define INT2VOIDP(i) (void *)(uintptr_t)(i)

static AddressSpace *remote_iommu_find_add_as(PCIBus *pci_bus,
                                              void *opaque, int devfn)
{
    struct RemoteIommuTable *iommu_table = opaque;
    struct RemoteIommuElem *elem = NULL;
    int pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_bus), devfn);

    elem = g_hash_table_lookup(iommu_table->elem_by_bdf, INT2VOIDP(pci_bdf));

    if (!elem) {
        g_autofree char *mr_name = g_strdup_printf("vfu-ram-%d", pci_bdf);
        g_autofree char *as_name = g_strdup_printf("vfu-as-%d", pci_bdf);

        elem = g_malloc0(sizeof(struct RemoteIommuElem));

        memory_region_init(&elem->mr, NULL, mr_name, UINT64_MAX);
        address_space_init(&elem->as, &elem->mr, as_name);

        qemu_mutex_lock(&iommu_table->lock);
        g_hash_table_insert(iommu_table->elem_by_bdf, INT2VOIDP(pci_bdf), elem);
        qemu_mutex_unlock(&iommu_table->lock);
    }

    return &elem->as;
}

static void remote_iommu_del_elem(gpointer data)
{
    struct RemoteIommuElem *elem = data;

    g_assert(elem);

    memory_region_unref(&elem->mr);
    address_space_destroy(&elem->as);

    g_free(elem);
}

void remote_iommu_del_device(PCIDevice *pci_dev)
{
    int pci_bdf;

    if (!remote_iommu_table.elem_by_bdf || !pci_dev) {
        return;
    }

    pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)), pci_dev->devfn);

    qemu_mutex_lock(&remote_iommu_table.lock);
    g_hash_table_remove(remote_iommu_table.elem_by_bdf, INT2VOIDP(pci_bdf));
    qemu_mutex_unlock(&remote_iommu_table.lock);
}

void remote_configure_iommu(PCIBus *pci_bus)
{
    if (!remote_iommu_table.elem_by_bdf) {
        remote_iommu_table.elem_by_bdf =
            g_hash_table_new_full(NULL, NULL, NULL, remote_iommu_del_elem);
        qemu_mutex_init(&remote_iommu_table.lock);
    }

    pci_setup_iommu(pci_bus, remote_iommu_find_add_as, &remote_iommu_table);
}
