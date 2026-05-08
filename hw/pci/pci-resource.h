/*
 * Copyright (C) 2026 NVIDIA
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_PCI_RESOURCE_H
#define HW_PCI_PCI_RESOURCE_H

#include "exec/hwaddr.h"
#include "hw/pci/pci.h"
#include <glib.h>

#define IORESOURCE_PREFETCH     0x00002000

typedef struct {
    uint64_t addr;
    uint64_t end;
    uint64_t flags;
} PhysBAR;

typedef struct {
    uint64_t wbase;
    uint64_t wlimit;
    uint64_t wbase64;
    uint64_t wlimit64;
    uint64_t rbase;
    uint64_t rlimit;
    uint64_t rsize;
    uint64_t piobase;
    bool     available;
    bool     search_mmio64;
    PCIDevice *dev;
    PCIBus *bus;
    /* Allocator window (filled once from machine memmap) */
    hwaddr   mmio32_base;
    hwaddr   mmio32_size;
    hwaddr   mmio64_base;
    hwaddr   mmio64_size;
} PciAllocCfg;

typedef struct FixedClaim {
    uint64_t start;
    uint64_t end;
    PCIDevice *owner;
    int bar;
} FixedClaim;

typedef struct {
    hwaddr mmio64_base;
    hwaddr mmio64_size;
    GHashTable *had_fixed; /* set of PCIDevice* that had at least one fixed BAR */
} PciProgramCtx;

typedef struct PciFixedBarMmioParams {
    hwaddr mmio32_base;
    hwaddr mmio32_size;
    hwaddr mmio64_base;
    hwaddr mmio64_size;
} PciFixedBarMmioParams;

void pci_fixed_bar_allocator(PCIBus *root, const PciFixedBarMmioParams *mmio);

#endif
