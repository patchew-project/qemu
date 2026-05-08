/*
 * Copyright (C) 2026 NVIDIA
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pci-resource.h"

/* Global list of claimed fixed 64-bit prefetchable BAR ranges */
static GArray *fixed_claim_regions;

static void fixed_claim_regions_reset(void)
{
    if (fixed_claim_regions) {
        g_array_free(fixed_claim_regions, true);
        fixed_claim_regions = NULL;
    }
    fixed_claim_regions = g_array_new(false, true, sizeof(FixedClaim));
}

static bool fixed_claim_regions_conflicts(uint64_t start, uint64_t end,
                                          uint64_t wbase64, uint64_t wlimit64,
                                          uint64_t *conflict_end)
{
    /* Hard guard: out-of-window ranges are invalid input */
    if (start < wbase64 || end > wlimit64) {
        error_report("placement [0x%"PRIx64"..0x%"PRIx64"] out of window "
                     "[0x%"PRIx64"..0x%"PRIx64"]",
                     start, end, wbase64, wlimit64);
        exit(1);
    }
    if (!fixed_claim_regions) {
        return false;
    }
    for (guint i = 0; i < fixed_claim_regions->len; i++) {
        FixedClaim *c = &g_array_index(fixed_claim_regions, FixedClaim, i);
        if (ranges_overlap(start, end - start + 1, c->start, c->end - c->start + 1)) {
            if (conflict_end) {
                *conflict_end = c->end;
            }
            return true;
        }
    }
    return false;
}

static void fixed_claim_regions_add(uint64_t start, uint64_t end, PCIDevice *dev, int bar)
{
    FixedClaim cl = { .start = start, .end = end, .owner = dev, .bar = bar };
    g_array_append_val(fixed_claim_regions, cl);
}

static void pci_validate_fixed_bar(PCIDevice *dev, int bar_index,
                                   uint64_t addr, uint64_t size,
                                   uint64_t wbase64, uint64_t wlimit64)
{
    PCIIORegion *r = &dev->io_regions[bar_index];
    uint64_t end;

    if (!r->size || !(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64)) {
        error_report("Invalid fixed-bars for %s [%02x:%02x.%x] BAR%d: "
                     "BAR not 64-bit or size=0 (type=0x%x size=0x%"PRIx64")",
                     dev->name, pci_dev_bus_num(dev),
                     PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
                     bar_index, r->type, (uint64_t)r->size);
        exit(1);
    }
    /* This path only programs 64-bit prefetchable MMIO in the MMIO64 window. */
    if (!(r->type & PCI_BASE_ADDRESS_MEM_PREFETCH) &&
        !pci_bus_is_root(pci_get_bus(dev))) {
        error_report("Invalid fixed-bars for %s [%02x:%02x.%x] BAR%d: "
                     "this allocator only supports 64-bit prefetchable MMIO; "
                     "64-bit non-prefetchable is not supported",
                     dev->name, pci_dev_bus_num(dev),
                     PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), bar_index);
        exit(1);
    }
    end = addr + size - 1;
    if (addr & (size - 1)) {
        error_report("Invalid fixed-bars alignment for %s [%02x:%02x.%x] "
                     "BAR%d: addr=0x%"PRIx64" size=0x%"PRIx64,
                     dev->name, pci_dev_bus_num(dev),
                     PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
                     bar_index, addr, size);
        exit(1);
    }
    if (addr < wbase64 || end > wlimit64) {
        error_report("fixed-bars out of window for %s [%02x:%02x.%x] BAR%d "
                     "range=[0x%"PRIx64"..0x%"PRIx64"] window=[0x%"PRIx64"..0x%"PRIx64"]",
                     dev->name, pci_dev_bus_num(dev),
                     PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
                     bar_index, addr, end, wbase64, wlimit64);
        exit(1);
    }
}

static void pci_check_fixed_bar_overlap(PCIDevice *dev, PhysBAR *pbars)
{
    for (int i = 0; i < PCI_ROM_SLOT; i++) {
        if (!(pbars[i].flags & IORESOURCE_PREFETCH)) {
            continue;
        }
        for (int j = i + 1; j < PCI_ROM_SLOT; j++) {
            if (!(pbars[j].flags & IORESOURCE_PREFETCH)) {
                continue;
            }
            if (ranges_overlap(pbars[i].addr, dev->io_regions[i].size,
                               pbars[j].addr, dev->io_regions[j].size)) {
                error_report("Invalid fixed-bars — fixed BAR overlap on %s [%02x:%02x.%x]: "
                             "BAR%d [0x%lx..0x%lx] vs BAR%d [0x%lx..0x%lx]",
                             dev->name, pci_dev_bus_num(dev),
                             PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
                             i, pbars[i].addr, pbars[i].addr + dev->io_regions[i].size - 1,
                             j, pbars[j].addr, pbars[j].addr + dev->io_regions[j].size - 1);
                exit(1);
            }
        }
    }
}

/* Program 64-bit prefetchable BARs */
static void pci_program_prefetch_bars(PCIDevice *dev, PhysBAR *pbars)
{
    int idx;
    uint32_t laddr;

    for (idx = 0; idx < PCI_ROM_SLOT; idx++) {
        PhysBAR *pbar = &pbars[idx];

        if (!(pbar->flags & IORESOURCE_PREFETCH)) {
            continue;
        }
        laddr = pbar->addr & PCI_BASE_ADDRESS_MEM_MASK;
        laddr |= PCI_BASE_ADDRESS_MEM_TYPE_64;
        /* Set PREFETCH bit only if the BAR itself is prefetchable */
        if (dev->io_regions[idx].type & PCI_BASE_ADDRESS_MEM_PREFETCH) {
            laddr |= PCI_BASE_ADDRESS_MEM_PREFETCH;
        }

        pci_host_config_write_common(dev,
                                     PCI_BASE_ADDRESS_0 + (idx * 4),
                                     pci_config_size(dev),
                                     laddr,
                                     4);
        pci_host_config_write_common(dev,
                                     PCI_BASE_ADDRESS_0 + (idx * 4) + 4,
                                     pci_config_size(dev),
                                     (uint32_t)(pbar->addr >> 32),
                                     4);
    }
}

/* Phase 1: claim and program fixed BARs for one device (per-device callback) */
static void pci_dev_claim_and_program_fixed_bars(PCIBus *bus, PCIDevice *dev, void *opaque)
{
    PciProgramCtx *pctx = (PciProgramCtx *)opaque;
    PhysBAR *pbar, pbars[PCI_ROM_SLOT];
    bool had_any_fixed = false;
    uint64_t start;
    uint64_t end;
    int idx;

    pbar = pbars;
    memset(pbar, 0, sizeof(pbars));

    if (!dev->fixed_bar_addrs) {
        return;
    }
    for (idx = 0; idx < PCI_ROM_SLOT; idx++) {
        PCIIORegion *r = &dev->io_regions[idx];
        if (dev->fixed_bar_addrs[idx] == PCI_BAR_UNMAPPED) {
            continue;
        }
        pci_validate_fixed_bar(dev, idx,
                                    dev->fixed_bar_addrs[idx],
                                    r->size,
                                    pctx->mmio64_base,
                                    pctx->mmio64_base + pctx->mmio64_size - 1);

        start = dev->fixed_bar_addrs[idx];
        end = start + r->size - 1;
        if (fixed_claim_regions_conflicts(start, end,
                                            pctx->mmio64_base,
                                            pctx->mmio64_base + pctx->mmio64_size - 1,
                                            NULL)) {
            error_report("Invalid fixed-bars — fixed BAR for %s [%02x:%02x.%x] "
                         "BAR%d [0x%"PRIx64"..0x%"PRIx64"] overlaps an existing fixed range",
                         dev->name, pci_dev_bus_num(dev),
                         PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
                         idx, start, end);
            exit(1);
        }
        fixed_claim_regions_add(start, end, dev, idx);
        pbars[idx].addr = dev->fixed_bar_addrs[idx];
        pbars[idx].end = pbars[idx].addr + r->size - 1;
        pbars[idx].flags = IORESOURCE_PREFETCH;
        had_any_fixed = true;
    }
    if (had_any_fixed) {
        g_hash_table_insert(pctx->had_fixed, dev, dev);
    }
    /* Abort if intra-device fixed overlap */
    pci_check_fixed_bar_overlap(dev, pbars);
    /* Program fixed BARs now */
    pci_program_prefetch_bars(dev, pbars);
}

static void pci_bus_claim_and_program_fixed_bars(PCIBus *bus, void *opaque)
{
    pci_for_each_device_under_bus(bus, pci_dev_claim_and_program_fixed_bars, opaque);
}

static void pci_resource_init_from_mmio(PciAllocCfg *pci_res,
                                   const PciFixedBarMmioParams *mmio)
{
    pci_res->mmio32_base = mmio->mmio32_base;
    pci_res->mmio32_size = mmio->mmio32_size;
    pci_res->mmio64_base = mmio->mmio64_base;
    pci_res->mmio64_size = mmio->mmio64_size;
}

void pci_fixed_bar_allocator(PCIBus *root, const PciFixedBarMmioParams *mmio)
{
    PciAllocCfg pci_res_buf, *pci_res = &pci_res_buf;
    PCIBus *bus = root;

    /* Fill allocator MMIO window once from machine memmap */
    pci_resource_init_from_mmio(pci_res, mmio);

    /* Reset fixed-claims tracking */
    fixed_claim_regions_reset();

    PciProgramCtx pctx = {
        .mmio64_base = pci_res->mmio64_base,
        .mmio64_size = pci_res->mmio64_size,
        .had_fixed = g_hash_table_new(NULL, NULL),
    };

    /* Phase 1: program all fixed BARs and claim them */
    pci_for_each_bus(bus, pci_bus_claim_and_program_fixed_bars, &pctx);

    /* TODOs: Phases 2–3, program remaining BARs, bridge window refresh etc,.  */

    /* Cleanup */
    g_hash_table_destroy(pctx.had_fixed);
    fixed_claim_regions_reset();
}
