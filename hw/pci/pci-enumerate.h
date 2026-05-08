/*
 * Copyright (C) 2026 NVIDIA
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_PCI_ENUMERATE_H
#define HW_PCI_PCI_ENUMERATE_H

#include "hw/pci/pci_bus.h"

void pci_enumerate_bus(PCIBus *root_bus);

#endif
