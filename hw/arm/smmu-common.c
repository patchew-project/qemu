/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Prem Mallappa <pmallapp@broadcom.com>
 *
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "exec/target_page.h"
#include "qom/cpu.h"

#include "qemu/error-report.h"
#include "hw/arm/smmu-common.h"

inline MemTxResult smmu_read_sysmem(dma_addr_t addr, void *buf, dma_addr_t len,
                                    bool secure)
{
    MemTxAttrs attrs = {.unspecified = 1, .secure = secure};

    switch (len) {
    case 4:
        *(uint32_t *)buf = ldl_le_phys(&address_space_memory, addr);
        break;
    case 8:
        *(uint64_t *)buf = ldq_le_phys(&address_space_memory, addr);
        break;
    default:
        return address_space_rw(&address_space_memory, addr,
                                attrs, buf, len, false);
    }
    return MEMTX_OK;
}

inline void
smmu_write_sysmem(dma_addr_t addr, void *buf, dma_addr_t len, bool secure)
{
    MemTxAttrs attrs = {.unspecified = 1, .secure = secure};

    switch (len) {
    case 4:
        stl_le_phys(&address_space_memory, addr, *(uint32_t *)buf);
        break;
    case 8:
        stq_le_phys(&address_space_memory, addr, *(uint64_t *)buf);
        break;
    default:
        address_space_rw(&address_space_memory, addr,
                         attrs, buf, len, true);
    }
}

/******************/
/* Infrastructure */
/******************/

static inline gboolean smmu_uint64_equal(gconstpointer v1, gconstpointer v2)
{
    return *((const uint64_t *)v1) == *((const uint64_t *)v2);
}

static inline guint smmu_uint64_hash(gconstpointer v)
{
    return (guint)*(const uint64_t *)v;
}

SMMUPciBus *smmu_find_as_from_bus_num(SMMUState *s, uint8_t bus_num)
{
    SMMUPciBus *smmu_pci_bus = s->smmu_as_by_bus_num[bus_num];

    if (!smmu_pci_bus) {
        GHashTableIter iter;

        g_hash_table_iter_init(&iter, s->smmu_as_by_busptr);
        while (g_hash_table_iter_next(&iter, NULL, (void **)&smmu_pci_bus)) {
            if (pci_bus_num(smmu_pci_bus->bus) == bus_num) {
                s->smmu_as_by_bus_num[bus_num] = smmu_pci_bus;
                return smmu_pci_bus;
            }
        }
    }
    return smmu_pci_bus;
}

static AddressSpace *smmu_find_add_as(PCIBus *bus, void *opaque, int devfn)
{
    SMMUState *s = opaque;
    uintptr_t key = (uintptr_t)bus;
    SMMUPciBus *sbus = g_hash_table_lookup(s->smmu_as_by_busptr, &key);
    SMMUDevice *sdev;

    if (!sbus) {
        uintptr_t *new_key = g_malloc(sizeof(*new_key));

        *new_key = (uintptr_t)bus;
        sbus = g_malloc0(sizeof(SMMUPciBus) +
                         sizeof(SMMUDevice *) * SMMU_PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->smmu_as_by_busptr, new_key, sbus);
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        char *name = g_strdup_printf("%s-%d-%d",
                                     s->mrtypename,
                                     pci_bus_num(bus), devfn);
        sdev = sbus->pbdev[devfn] = g_malloc0(sizeof(SMMUDevice));

        sdev->smmu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        memory_region_init_iommu(&sdev->iommu, sizeof(sdev->iommu),
                                 s->mrtypename,
                                 OBJECT(s), name, 1ULL << 48);
        address_space_init(&sdev->as,
                           MEMORY_REGION(&sdev->iommu), name);
    }

    return &sdev->as;
}

static void smmu_init_iommu_as(SMMUState *s)
{
    PCIBus *pcibus = pci_find_primary_bus();

    if (pcibus) {
        pci_setup_iommu(pcibus, smmu_find_add_as, s);
    } else {
        error_report("No PCI bus, SMMU is not registered");
    }
}

static void smmu_base_instance_init(Object *obj)
{
    SMMUState *s = SMMU_SYS_DEV(obj);

    memset(s->smmu_as_by_bus_num, 0, sizeof(s->smmu_as_by_bus_num));

    s->smmu_as_by_busptr = g_hash_table_new_full(smmu_uint64_hash,
                                                 smmu_uint64_equal,
                                                 g_free, g_free);
    smmu_init_iommu_as(s);
}

static void smmu_base_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo smmu_base_info = {
    .name          = TYPE_SMMU_DEV_BASE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SMMUState),
    .instance_init = smmu_base_instance_init,
    .class_data    = NULL,
    .class_size    = sizeof(SMMUBaseClass),
    .class_init    = smmu_base_class_init,
    .abstract      = true,
};

static void smmu_base_register_types(void)
{
    type_register_static(&smmu_base_info);
}

type_init(smmu_base_register_types)

