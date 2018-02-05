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
#include "hw/qdev-properties.h"
#include "qapi/error.h"

#include "qemu/error-report.h"
#include "hw/arm/smmu-common.h"

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
    SMMUPciBus *sbus = g_hash_table_lookup(s->smmu_as_by_busptr, &bus);
    SMMUDevice *sdev;

    if (!sbus) {
        sbus = g_malloc0(sizeof(SMMUPciBus) +
                         sizeof(SMMUDevice *) * SMMU_PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->smmu_as_by_busptr, bus, sbus);
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        char *name = g_strdup_printf("%s-%d-%d",
                                     s->mrtypename,
                                     pci_bus_num(bus), devfn);
        sdev = sbus->pbdev[devfn] = g_new0(SMMUDevice, 1);

        sdev->smmu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        memory_region_init_iommu(&sdev->iommu, sizeof(sdev->iommu),
                                 s->mrtypename,
                                 OBJECT(s), name, 1ULL << SMMU_MAX_VA_BITS);
        address_space_init(&sdev->as,
                           MEMORY_REGION(&sdev->iommu), name);
    }

    return &sdev->as;
}

static void smmu_base_realize(DeviceState *dev, Error **errp)
{
    SMMUState *s = ARM_SMMU(dev);

    s->smmu_as_by_busptr = g_hash_table_new(NULL, NULL);

    if (s->primary_bus) {
        pci_setup_iommu(s->primary_bus, smmu_find_add_as, s);
    } else {
        error_setg(errp, "SMMU is not attached to any PCI bus!");
    }
}

static Property smmu_dev_properties[] = {
    DEFINE_PROP_UINT8("bus_num", SMMUState, bus_num, 0),
    DEFINE_PROP_LINK("primary-bus", SMMUState, primary_bus, "PCI", PCIBus *),
    DEFINE_PROP_END_OF_LIST(),
};

static void smmu_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMMUBaseClass *sbc = ARM_SMMU_CLASS(klass);

    dc->props = smmu_dev_properties;
    sbc->parent_realize = dc->realize;
    dc->realize = smmu_base_realize;
}

static const TypeInfo smmu_base_info = {
    .name          = TYPE_ARM_SMMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SMMUState),
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

