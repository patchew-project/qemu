/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * BAR address validation for fixed-BAR placement.
 *
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci/pci_host.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "pci-internal.h"
#include "pci-fixed-bar-validate.h"

/*
 * Claimed BAR ranges — detect inter-device and inter-hierarchy overlaps
 * across all fixed BARs system-wide.
 */

typedef struct {
    uint64_t    start;
    uint64_t    end;
    const char *owner;  /* device name, for error messages */
    int         bar;
} FixedClaim;

static GArray *fixed_claims;

static void fixed_claims_init(void)
{
    if (fixed_claims) {
        g_array_free(fixed_claims, true);
    }
    fixed_claims = g_array_new(false, true, sizeof(FixedClaim));
}

static void fixed_claims_free(void)
{
    g_array_free(fixed_claims, true);
    fixed_claims = NULL;
}

static bool fixed_claims_overlap(uint64_t start, uint64_t end,
                                 const char **owner_out, int *bar_out,
                                 uint64_t *start_out, uint64_t *end_out)
{
    FixedClaim *c;
    guint i;

    for (i = 0; i < fixed_claims->len; i++) {
        c = &g_array_index(fixed_claims, FixedClaim, i);
        if (ranges_overlap(start, end - start + 1,
                           c->start, c->end - c->start + 1)) {
            *owner_out = c->owner;
            *bar_out   = c->bar;
            *start_out = c->start;
            *end_out   = c->end;
            return true;
        }
    }
    return false;
}

static void fixed_claims_add(uint64_t start, uint64_t end,
                             const char *owner, int bar)
{
    FixedClaim cl;

    cl.start = start;
    cl.end   = end;
    cl.owner = owner;
    cl.bar   = bar;
    g_array_append_val(fixed_claims, cl);
}

static bool validate_bars(PCIDevice *pdev, FixedBarsInfo *info,
                          bool is_fixed_subtree)
{
    const char  *devname = DEVICE(pdev)->id ? DEVICE(pdev)->id : pdev->name;
    const char  *overlap_owner;
    const char  *wname;
    bool         has_pci_bars = (pdev->fixed_bar_addrs != NULL);
    bool         is_64bit;
    uint64_t     addr, end, wbase, wlim, overlap_start, overlap_end;
    PCIIORegion *r;
    int          overlap_bar;
    int          i;

    if (is_fixed_subtree && !has_pci_bars) {
        error_report("pci-bars: %s [%02x:%02x.%x] under fixed-bar root port "
                     "has memory BARs but no pci-bars= specified",
                     devname, pci_dev_bus_num(pdev),
                     PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
        exit(1);
    }

    if (!has_pci_bars) {
        return false;
    }

    /* Completeness: every memory BAR must have an address. */
    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (!r->size || (r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            continue;
        }
        if (pdev->fixed_bar_addrs[i] == PCI_BAR_UNMAPPED) {
            error_report("pci-bars: %s [%02x:%02x.%x] BAR%d "
                         "missing from pci-bars=",
                         devname, pci_dev_bus_num(pdev),
                         PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn), i);
            exit(1);
        }
    }

    /* Per-BAR checks: alignment, MMIO window, overlap. */
    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (!r->size || (r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            continue;
        }

        is_64bit = !!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64);
        addr     = (uint64_t)pdev->fixed_bar_addrs[i];

        if (r->size - 1 > UINT64_MAX - addr) {
            error_report("pci-bars: %s [%02x:%02x.%x] BAR%d "
                         "addr=0x%"PRIx64" + size=0x%"PRIx64" overflows",
                         devname, pci_dev_bus_num(pdev),
                         PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                         i, addr, r->size);
            exit(1);
        }
        end = addr + r->size - 1;

        /*
         * wbase/wlim are both inclusive bounds (mmio*_limit = base+size-1,
         * set by the caller alongside mmio*_base), matching end's own
         * inclusive computation above.
         */
        if (is_64bit) {
            wbase = info->mmio64_base;
            wlim  = info->mmio64_limit;
            wname = "64-bit MMIO";
        } else {
            wbase = info->mmio32_base;
            wlim  = info->mmio32_limit;
            wname = "32-bit MMIO";
        }

        if (addr & (r->size - 1)) {
            error_report("pci-bars: %s [%02x:%02x.%x] BAR%d "
                         "addr=0x%"PRIx64" not aligned to size=0x%"PRIx64,
                         devname, pci_dev_bus_num(pdev),
                         PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                         i, addr, r->size);
            exit(1);
        }

        if (addr < wbase || end > wlim) {
            error_report("pci-bars: %s [%02x:%02x.%x] BAR%d "
                         "[0x%"PRIx64"..0x%"PRIx64"] outside %s window "
                         "[0x%"PRIx64"..0x%"PRIx64"]",
                         devname, pci_dev_bus_num(pdev),
                         PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                         i, addr, end, wname, wbase, wlim);
            exit(1);
        }

        if (fixed_claims_overlap(addr, end, &overlap_owner, &overlap_bar,
                                 &overlap_start, &overlap_end)) {
            error_report("pci-bars: %s [%02x:%02x.%x] BAR%d "
                         "[0x%"PRIx64"..0x%"PRIx64"] overlaps %s BAR%d "
                         "[0x%"PRIx64"..0x%"PRIx64"]",
                         devname, pci_dev_bus_num(pdev),
                         PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                         i, addr, end, overlap_owner, overlap_bar,
                         overlap_start, overlap_end);
            exit(1);
        }

        fixed_claims_add(addr, end, devname, i);
    }

    return true;
}

/*
 * True if bus, or any of its ancestor buses, hangs off a fixed-bar=on
 * root port. Walks up via bus->parent_dev / pci_get_bus().
 */
static bool bus_is_fixed_subtree(PCIBus *bus)
{
    PCIDevice *parent;

    while (bus) {
        parent = bus->parent_dev;
        if (!parent) {
            return false;
        }
        if (object_dynamic_cast(OBJECT(parent), TYPE_PCIE_ROOT_PORT) &&
            PCIE_SLOT(parent)->fixed_bar) {
            return true;
        }
        bus = pci_get_bus(parent);
    }
    return false;
}

static void scan_bus(PCIBus *bus, void *opaque);

static void scan_bus_device(PCIBus *bus, PCIDevice *pdev, void *opaque)
{
    FixedBarsInfo *info = opaque;
    bool has_mem_bar = false;
    PCIIORegion *r;
    PCIBus *sec;
    int i;

    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (r->size && !(r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            has_mem_bar = true;
            break;
        }
    }

    if (has_mem_bar) {
        if (validate_bars(pdev, info, bus_is_fixed_subtree(bus))) {
            info->any_fixed = true;
        }
    }

    if (!object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE)) {
        return;
    }
    sec = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (sec) {
        scan_bus(sec, opaque);
    }
}

static void scan_bus(PCIBus *bus, void *opaque)
{
    pci_for_each_device_under_bus(bus, scan_bus_device, opaque);
}

static void scan_all_host_bridges(FixedBarsInfo *info)
{
    PCIHostState *hb;

    fixed_claims_init();
    QLIST_FOREACH(hb, &pci_host_bridges, next) {
        if (hb->bus) {
            scan_bus(hb->bus, info);
        }
    }
    fixed_claims_free();
}

/*
 * fixed_bars_validate - scan all PCI devices, validate fixed BAR addresses.
 *
 * @info: carries MMIO window bounds for validation.
 *
 * Returns true if any fixed BAR devices were found, false if there is
 * nothing to do. Aborts on any validation error.
 */
bool fixed_bars_validate(FixedBarsInfo *info)
{
    info->any_fixed = false;
    scan_all_host_bridges(info);
    return info->any_fixed;
}
