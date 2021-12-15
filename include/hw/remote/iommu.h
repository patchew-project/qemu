/*
 * IOMMU for remote device
 *
 * Copyright © 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_IOMMU_H
#define REMOTE_IOMMU_H

#include "hw/pci/pci_bus.h"

void remote_iommu_free(PCIDevice *pci_dev);

void remote_iommu_init(void);

void remote_iommu_set(PCIBus *bus);

MemoryRegion *remote_iommu_get_ram(PCIDevice *pci_dev);

#endif
