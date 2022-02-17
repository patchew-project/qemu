/**
 * Copyright © 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_IOMMU_H
#define REMOTE_IOMMU_H

#include "hw/pci/pci_bus.h"

void remote_configure_iommu(PCIBus *pci_bus);

void remote_iommu_del_device(PCIDevice *pci_dev);

#endif
