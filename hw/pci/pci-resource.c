/*
 * Copyright (C) 2026 NVIDIA
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
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

static void pci_update_prefetch_window(PCIBus *bus, uint64_t base, uint64_t limit)
{
    PCIDevice *bridge = pci_bridge_get_device(bus);
    uint32_t reg_base, reg_limit;

    assert(bridge);

    reg_base = (uint32_t)(extract64(base, 20, 12) << 4);
    reg_limit = (uint32_t)(extract64(limit, 20, 12) << 4);
    pci_host_config_write_common(bridge,
                                 PCI_PREF_MEMORY_BASE,
                                 pci_config_size(bridge),
                                 reg_base | PCI_PREF_RANGE_TYPE_64,
                                 2);
    pci_host_config_write_common(bridge,
                                 PCI_PREF_BASE_UPPER32,
                                 pci_config_size(bridge),
                                 (uint32_t)(base >> 32),
                                 4);
    pci_host_config_write_common(bridge,
                                 PCI_PREF_MEMORY_LIMIT,
                                 pci_config_size(bridge),
                                 reg_limit | PCI_PREF_RANGE_TYPE_64,
                                 2);
    pci_host_config_write_common(bridge,
                                 PCI_PREF_LIMIT_UPPER32,
                                 pci_config_size(bridge),
                                 (uint32_t)(limit >> 32),
                                 4);
}

static inline bool is_64bit_pref_bar(PCIIORegion *r)
{
    if (!r->size) {
        return false;
    }
    if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
        return false;
    }
    if (!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64)) {
        return false;
    }
    if (!(r->type & PCI_BASE_ADDRESS_MEM_PREFETCH)) {
        return false;
    }
    return true;
}

/* Comparison function for sorting intervals by start address */
static int compare_intervals(gconstpointer a, gconstpointer b)
{
    const AddressInterval *ia = (const AddressInterval *)a;
    const AddressInterval *ib = (const AddressInterval *)b;
    if (ia->start < ib->start) return -1;
    if (ia->start > ib->start) return 1;
    return 0;
}

/* Comparison function for sorting BARs by descending size */
static int compare_bar_size_desc(gconstpointer a, gconstpointer b)
{
    const BarEntry *ea = (const BarEntry *)a;
    const BarEntry *eb = (const BarEntry *)b;
    if (ea->size > eb->size) return -1;
    if (ea->size < eb->size) return 1;
    return 0;
}

/* Categorize holes relative to anchors */
static CategorizedHoles categorize_holes(GArray *holes, GArray *fixed_bars)
{
    CategorizedHoles result = {
        .leftmost_hole = -1,
        .middle_holes = g_array_new(false, false, sizeof(int)),
        .rightmost_hole = -1
    };

    /* Get anchor boundaries */
    uint64_t first_anchor_start = g_array_index(fixed_bars, AddressInterval, 0).start;
    uint64_t last_anchor_end = g_array_index(fixed_bars, AddressInterval,
                                             fixed_bars->len - 1).end;
    /* Categorize each hole */
    for (guint h = 0; h < holes->len; h++) {
        AddressInterval *hole = &g_array_index(holes, AddressInterval, h);

        if (hole->end < first_anchor_start) {
            result.leftmost_hole = h;  /* Before all anchors */
        } else if (hole->start > last_anchor_end) {
            result.rightmost_hole = h;  /* After all anchors */
        } else {
            g_array_append_val(result.middle_holes, h);  /* Between anchors */
        }
    }
    return result;
}

/*
 * Compute REAL holes considering both local anchors and global claims.
 * This returns actual free space that can be used for packing.
 * Strategy: Collect all obstacles (local fixed BARs + global claims from
 * other buses), then compute gaps between them.
 */
static GArray* compute_real_holes(GArray *fixed_bars, uint64_t mmio_start, uint64_t mmio_end)
{
    GArray *holes = g_array_new(false, false, sizeof(AddressInterval));
    GArray *claimed_regions = g_array_new(false, false, sizeof(AddressInterval));
    uint64_t scan;

    /* Add local fixed BARs (anchors) as claimed regions */
    for (guint i = 0; i < fixed_bars->len; i++) {
        AddressInterval *anchor = &g_array_index(fixed_bars, AddressInterval, i);
        g_array_append_val(claimed_regions, *anchor);
    }

    /* Add global claims from ALL buses (including other buses) */
    if (fixed_claim_regions) {
        for (guint i = 0; i < fixed_claim_regions->len; i++) {
            FixedClaim *claim = &g_array_index(fixed_claim_regions, FixedClaim, i);
            /* Only consider claims within our MMIO window */
            if (claim->start <= mmio_end && claim->end >= mmio_start) {
                AddressInterval region = {
                    .start = claim->start,
                    .end = claim->end
                };
                g_array_append_val(claimed_regions, region);
            }
        }
    }

    /* Handle case with no claimed regions */
    if (claimed_regions->len == 0) {
        AddressInterval hole = { .start = mmio_start, .end = mmio_end };
        g_array_append_val(holes, hole);
        g_array_free(claimed_regions, true);
        return holes;
    }

    /* Sort claimed regions by start address */
    g_array_sort(claimed_regions, compare_intervals);

    /* Compute holes between all claimed regions */
    scan = mmio_start;
    for (guint i = 0; i < claimed_regions->len; i++) {
        AddressInterval *claimed = &g_array_index(claimed_regions, AddressInterval, i);

        /* Free space before this claimed region */
        if (scan < claimed->start) {
            AddressInterval hole = { .start = scan, .end = claimed->start - 1 };
            g_array_append_val(holes, hole);
        }

        /* Move scan cursor past this claimed region */
        scan = MAX(scan, claimed->end + 1);
    }

    /* Free space after last claimed region */
    if (scan <= mmio_end) {
        AddressInterval hole = { .start = scan, .end = mmio_end };
        g_array_append_val(holes, hole);
    }

    g_array_free(claimed_regions, true);
    return holes;
}

static bool pack_bars_into_region(GArray *bars, uint64_t pack_start, uint64_t pack_end,
                                   uint64_t *out_min_addr, uint64_t *out_max_addr)
{
    uint64_t pack_cursor = pack_start;
    uint64_t min_addr = UINT64_MAX;
    uint64_t max_addr = 0;

    for (guint i = 0; i < bars->len; i++) {
        BarEntry *e = &g_array_index(bars, BarEntry, i);
        PCIIORegion *r = &e->dev->io_regions[e->bar_idx];

        uint64_t aligned_addr = ROUND_UP(pack_cursor, r->size);
        uint64_t bar_start = aligned_addr;
        uint64_t bar_end = bar_start + r->size - 1;

        if (bar_end > pack_end) {
            return false; /* Doesn't fit */
        }

        PhysBAR pbars_array[PCI_ROM_SLOT];
        memset(pbars_array, 0, sizeof(pbars_array));
        pbars_array[e->bar_idx].addr = bar_start;
        pbars_array[e->bar_idx].end = bar_end;
        pbars_array[e->bar_idx].flags = IORESOURCE_PREFETCH;

        pci_program_prefetch_bars(e->dev, pbars_array);

        min_addr = MIN(min_addr, bar_start);
        max_addr = MAX(max_addr, bar_end);
        pack_cursor = bar_end + 1;
    }

    *out_min_addr = min_addr;
    *out_max_addr = max_addr;
    return true;
}

static void finalize_bridge_window(PCIBus *bus, uint64_t min_addr, uint64_t max_addr)
{
    PCIDevice *bridge_dev = pci_bridge_get_device(bus);

    if (bridge_dev) {
        fixed_claim_regions_add(min_addr, max_addr, bridge_dev, -1);
        pci_update_prefetch_window(bus, min_addr, max_addr);
    }
}

static bool pci_bus_phase2_fill_bar_lists(PCIBus *bus, PciProgramCtx *pctx,
                                          GArray *fixed_bars, GArray *remaining_bars)
{
    AddressInterval interval;
    BarEntry bentry;
    PCIDevice *d;
    PCIIORegion *r;
    bool bus_has_fixed = false;
    bool device_has_fixed;
    int devfn, i;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        d = bus->devices[devfn];
        if (!d) {
            continue;
        }
        device_has_fixed = g_hash_table_contains(pctx->had_fixed, d);
        if (device_has_fixed) {
            bus_has_fixed = true;
        }
        for (i = 0; i < PCI_ROM_SLOT; i++) {
            r = &d->io_regions[i];
            if (!is_64bit_pref_bar(r)) {
                continue;
            }
            if (device_has_fixed && d->fixed_bar_addrs &&
                d->fixed_bar_addrs[i] != PCI_BAR_UNMAPPED) {
                interval.start = d->fixed_bar_addrs[i];
                interval.end = d->fixed_bar_addrs[i] + r->size - 1;
                g_array_append_val(fixed_bars, interval);
            } else {
                bentry.dev = d;
                bentry.bar_idx = i;
                bentry.size = r->size;
                g_array_append_val(remaining_bars, bentry);
            }
        }
    }
    return bus_has_fixed;
}

/* Find a mmio64 hole, pack unassigned BARs and program the bridge */
static void
pci_bus_phase2_hole_pack_and_update_bridge(PCIBus *bus, GArray *fixed_bars,
                                           GArray *remaining_bars,
                                           uint64_t mmio_start,
                                           uint64_t mmio_end)
{
    GArray *holes;
    FixedClaim *claim;
    CategorizedHoles cat;
    AddressInterval *holep, *selected;
    int selected_hole, largest_middle, h_idx;
    guint c, mid_i, f;
    uint64_t bus_min_addr, bus_max_addr, remaining_demand;
    uint64_t leftmost_anchor, rightmost_anchor_end, valid_start, valid_end;
    uint64_t largest_size, hole_size, pack_start, pack_end;

    g_array_sort(fixed_bars, compare_intervals);
    g_array_sort(remaining_bars, compare_bar_size_desc);

    remaining_demand = 0;
    for (c = 0; c < remaining_bars->len; c++) {
        remaining_demand += g_array_index(remaining_bars, BarEntry, c).size;
    }

    leftmost_anchor = g_array_index(fixed_bars, AddressInterval, 0).start;
    rightmost_anchor_end = g_array_index(fixed_bars, AddressInterval,
                                        fixed_bars->len - 1).end;

    valid_start = mmio_start;
    valid_end = mmio_end;

    if (fixed_claim_regions) {
        for (c = 0; c < fixed_claim_regions->len; c++) {
            claim = &g_array_index(fixed_claim_regions, FixedClaim, c);
            if (claim->end < leftmost_anchor && claim->end >= valid_start) {
                valid_start = claim->end + 1;
            }
            if (claim->start > rightmost_anchor_end && claim->start <= valid_end) {
                valid_end = claim->start - 1;
            }
        }
    }

    holes = compute_real_holes(fixed_bars, valid_start, valid_end);
    cat = categorize_holes(holes, fixed_bars);

    selected_hole = -1;
    pack_start = 0;
    pack_end = 0;

    if (cat.middle_holes->len > 0) {
        largest_middle = -1;
        largest_size = 0;
        for (mid_i = 0; mid_i < cat.middle_holes->len; mid_i++) {
            h_idx = g_array_index(cat.middle_holes, int, mid_i);
            holep = &g_array_index(holes, AddressInterval, h_idx);
            hole_size = holep->end - holep->start + 1;
            if (hole_size >= remaining_demand && hole_size > largest_size) {
                largest_size = hole_size;
                largest_middle = h_idx;
            }
        }
        if (largest_middle >= 0) {
            selected_hole = largest_middle;
        }
    }
    if (selected_hole < 0 && cat.rightmost_hole >= 0) {
        holep = &g_array_index(holes, AddressInterval, cat.rightmost_hole);
        hole_size = holep->end - holep->start + 1;
        if (hole_size >= remaining_demand) {
            selected_hole = cat.rightmost_hole;
        }
    }
    if (selected_hole < 0 && cat.leftmost_hole >= 0) {
        holep = &g_array_index(holes, AddressInterval, cat.leftmost_hole);
        hole_size = holep->end - holep->start + 1;
        if (hole_size >= remaining_demand) {
            selected_hole = cat.leftmost_hole;
        }
    }
    g_array_free(cat.middle_holes, true);
    if (selected_hole < 0) {
        error_report("bus [%02x] insufficient contiguous space for "
                     "remaining_demand=0x%"PRIx64,
                     pci_bus_num(bus), remaining_demand);
        g_array_free(holes, true);
        g_array_free(fixed_bars, true);
        g_array_free(remaining_bars, true);
        exit(1);
    }
    selected = &g_array_index(holes, AddressInterval, selected_hole);
    pack_start = selected->start;
    pack_end = selected->end;
    g_array_free(holes, true);
    if (!pack_bars_into_region(remaining_bars, pack_start, pack_end,
                                 &bus_min_addr, &bus_max_addr)) {
        error_report("bus [%02x] failed to pack BARs", pci_bus_num(bus));
        g_array_free(fixed_bars, true);
        g_array_free(remaining_bars, true);
        exit(1);
    }
    for (f = 0; f < fixed_bars->len; f++) {
        holep = &g_array_index(fixed_bars, AddressInterval, f);
        bus_min_addr = MIN(bus_min_addr, holep->start);
        bus_max_addr = MAX(bus_max_addr, holep->end);
    }
    finalize_bridge_window(bus, bus_min_addr, bus_max_addr);
    g_array_free(fixed_bars, true);
    g_array_free(remaining_bars, true);
}

static void pci_bus_phase2_pack_remaining_bars(PCIBus *bus, void *opaque)
{
    PciProgramCtx *pctx = (PciProgramCtx *)opaque;
    GArray *fixed_bars, *remaining_bars;
    uint64_t mmio_start, mmio_end, bus_min_addr, bus_max_addr;
    bool bus_has_fixed;

    mmio_start = pctx->mmio64_base;
    mmio_end = pctx->mmio64_base + pctx->mmio64_size - 1;
    fixed_bars = g_array_new(false, false, sizeof(AddressInterval));
    remaining_bars = g_array_new(false, false, sizeof(BarEntry));
    bus_has_fixed = pci_bus_phase2_fill_bar_lists(bus, pctx, fixed_bars,
                                                    remaining_bars);
    if (!bus_has_fixed) {
        g_array_free(fixed_bars, true);
        g_array_free(remaining_bars, true);
        return;
    }
    if (remaining_bars->len == 0) {
        if (fixed_bars->len > 0) {
            g_array_sort(fixed_bars, compare_intervals);
            bus_min_addr = g_array_index(fixed_bars, AddressInterval, 0).start;
            bus_max_addr = g_array_index(fixed_bars, AddressInterval,
                                        fixed_bars->len - 1).end;
            finalize_bridge_window(bus, bus_min_addr, bus_max_addr);
        }
        g_array_free(fixed_bars, true);
        g_array_free(remaining_bars, true);
        return;
    }
    pci_bus_phase2_hole_pack_and_update_bridge(bus, fixed_bars, remaining_bars,
                                                mmio_start, mmio_end);
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

    /* Phase 2: pack remaining 64-bit prefetchable BARs and size parent bridge window */
    pci_for_each_bus(bus, pci_bus_phase2_pack_remaining_bars, &pctx);

    /* Phase 3: buses with no fixed-BAR devices; final bridge pass: follow-up */

    /* Cleanup */
    g_hash_table_destroy(pctx.had_fixed);
    fixed_claim_regions_reset();
}
