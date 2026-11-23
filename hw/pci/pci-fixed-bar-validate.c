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
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "qapi/error.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "pci-internal.h"
#include "pci-fixed-bar-validate.h"
#include "hw/pci/pci-fixed-bar.h"

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

static void fixed_claims_init(FixedBarsInfo *info)
{
    info->claims = g_array_new(false, true, sizeof(FixedClaim));
}

static void fixed_claims_free(FixedBarsInfo *info)
{
    g_array_free(info->claims, true);
    info->claims = NULL;
}

static bool fixed_claims_overlap(FixedBarsInfo *info,
                                 uint64_t start, uint64_t end,
                                 const char **owner_out, int *bar_out,
                                 uint64_t *start_out, uint64_t *end_out)
{
    FixedClaim *c;
    guint i;

    for (i = 0; i < info->claims->len; i++) {
        c = &g_array_index(info->claims, FixedClaim, i);
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

static void fixed_claims_add(FixedBarsInfo *info,
                             uint64_t start, uint64_t end,
                             const char *owner, int bar)
{
    FixedClaim cl;

    cl.start = start;
    cl.end   = end;
    cl.owner = owner;
    cl.bar   = bar;
    g_array_append_val(info->claims, cl);
}

/*
 * Check that the BAR [start..end] fits in the window [base..limit].
 * Both ranges are inclusive.
 */
static bool bar_in_window(uint64_t start, uint64_t end,
                          uint64_t base, uint64_t limit)
{
    return start >= base && end <= limit;
}

static bool validate_bars(PCIDevice *pdev, FixedBarsInfo *info,
                          bool is_fixed_subtree, Error **errp)
{
    const char  *devname = DEVICE(pdev)->id ? DEVICE(pdev)->id : pdev->name;
    const char  *overlap_owner;
    bool         has_fixed_bars = false;
    bool         is_64bit;
    uint64_t     addr, end, overlap_start, overlap_end;
    PCIIORegion *r;
    int          overlap_bar;
    int          i;

    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        if (pdev->fixed_bar_addr[i] != PCI_BAR_UNMAPPED) {
            has_fixed_bars = true;
            break;
        }
    }

    if (object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE)) {
        if (has_fixed_bars) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] is a PCI bridge, "
                       "fixed-bar-N is only supported on endpoint devices",
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
            return false;
        }
        return true;
    }

    if (is_fixed_subtree && !has_fixed_bars) {
        error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] under fixed-bar root "
                   "port has memory BARs but no fixed-bar-N= specified",
                   devname, pci_dev_bus_num(pdev),
                   PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
        return false;
    }

    if (!has_fixed_bars) {
        return true;
    }

    /* Completeness: every memory BAR must have an address. */
    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (!r->size || (r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            continue;
        }
        if (pdev->fixed_bar_addr[i] == PCI_BAR_UNMAPPED) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                       "missing a fixed-bar-N= value",
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn), i);
            return false;
        }
    }

    /* Per-BAR checks: alignment, MMIO window, overlap. */
    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (!r->size || (r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            continue;
        }

        is_64bit = !!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64);
        addr     = (uint64_t)pdev->fixed_bar_addr[i];

        if (r->size - 1 > UINT64_MAX - addr) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                       "addr=0x%"PRIx64" + size=0x%"PRIx64" overflows",
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                       i, addr, r->size);
            return false;
        }
        end = addr + r->size - 1;

        if (is_64bit && !(r->type & PCI_BASE_ADDRESS_MEM_PREFETCH) &&
            !pci_bus_is_root(pci_get_bus(pdev)) && end >= 4 * GiB) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d is a "
                       "non-prefetchable 64-bit BAR behind a PCI bridge "
                       "and cannot be placed above 4GB",
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn), i);
            return false;
        }

        if (addr & (r->size - 1)) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                       "addr=0x%"PRIx64" not aligned to size=0x%"PRIx64,
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                       i, addr, r->size);
            return false;
        }

        /*
         * A 32-bit BAR must fit in the 32-bit window; a 64-bit BAR in either,
         * except a prefetchable one, which must be above 4GiB.
         */
        if (is_64bit && (r->type & PCI_BASE_ADDRESS_MEM_PREFETCH)) {
            if (!bar_in_window(addr, end,
                               info->mmio64_base, info->mmio64_limit)) {
                error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                           "[0x%"PRIx64"..0x%"PRIx64"] outside the 64-bit "
                           "window [0x%"PRIx64"..0x%"PRIx64"]",
                           devname, pci_dev_bus_num(pdev),
                           PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                           i, addr, end,
                           info->mmio64_base, info->mmio64_limit);
                return false;
            }
        } else if (is_64bit) {
            if (!bar_in_window(addr, end,
                               info->mmio32_base, info->mmio32_limit) &&
                !bar_in_window(addr, end,
                               info->mmio64_base, info->mmio64_limit)) {
                error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                           "[0x%"PRIx64"..0x%"PRIx64"] outside both the "
                           "32-bit window [0x%"PRIx64"..0x%"PRIx64"] and "
                           "the 64-bit window [0x%"PRIx64"..0x%"PRIx64"]",
                           devname, pci_dev_bus_num(pdev),
                           PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                           i, addr, end,
                           info->mmio32_base, info->mmio32_limit,
                           info->mmio64_base, info->mmio64_limit);
                return false;
            }
        } else if (!bar_in_window(addr, end,
                                  info->mmio32_base, info->mmio32_limit)) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                       "[0x%"PRIx64"..0x%"PRIx64"] outside the 32-bit "
                       "window [0x%"PRIx64"..0x%"PRIx64"]",
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                       i, addr, end,
                       info->mmio32_base, info->mmio32_limit);
            return false;
        }

        if (fixed_claims_overlap(info, addr, end, &overlap_owner,
                                 &overlap_bar, &overlap_start, &overlap_end)) {
            error_setg(errp, "fixed-bar: %s [%02x:%02x.%x] BAR%d "
                       "[0x%"PRIx64"..0x%"PRIx64"] overlaps %s BAR%d "
                       "[0x%"PRIx64"..0x%"PRIx64"]",
                       devname, pci_dev_bus_num(pdev),
                       PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                       i, addr, end, overlap_owner, overlap_bar,
                       overlap_start, overlap_end);
            return false;
        }

        fixed_claims_add(info, addr, end, devname, i);
    }

    info->any_fixed = true;
    return true;
}

typedef struct {
    FixedBarsInfo *info;
    bool           is_fixed_subtree;
    Error         **errp;
} ScanState;

static void scan_bus(PCIBus *bus, ScanState *state);

static void mark_if_fixed(PCIBus *bus, PCIDevice *pdev, void *opaque)
{
    bool *found = opaque;
    PCIBus *sec;
    int i;

    if (*found) {
        return;
    }
    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        if (pdev->fixed_bar_addr[i] != PCI_BAR_UNMAPPED) {
            *found = true;
            return;
        }
    }
    if (object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE)) {
        sec = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
        if (sec) {
            pci_for_each_device_under_bus(sec, mark_if_fixed, found);
        }
    }
}

/*
 * True if any device in rp_bus's subtree (the bus behind a root port)
 * has a fixed BAR.
 */
static bool root_port_subtree_has_fixed_bar(PCIBus *rp_bus)
{
    bool found = false;

    pci_for_each_device_under_bus(rp_bus, mark_if_fixed, &found);
    return found;
}

static void scan_bus_device(PCIBus *bus, PCIDevice *pdev, void *opaque)
{
    ScanState   *state = opaque;
    bool         has_mem_bar = false;
    PCIIORegion *r;
    PCIBus      *sec;
    int          i;

    if (*state->errp) {
        return;
    }

    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (r->size && !(r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            has_mem_bar = true;
            break;
        }
    }

    if (has_mem_bar &&
        !validate_bars(pdev, state->info, state->is_fixed_subtree,
                       state->errp)) {
        return;
    }

    if (!object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE)) {
        return;
    }
    sec = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (!sec) {
        return;
    }

    if (object_dynamic_cast(OBJECT(pdev), TYPE_PCIE_ROOT_PORT)) {
        ScanState child = {
            .info = state->info,
            .is_fixed_subtree = root_port_subtree_has_fixed_bar(sec),
            .errp = state->errp,
        };
        scan_bus(sec, &child);
    } else {
        scan_bus(sec, state);
    }
}

static void scan_bus(PCIBus *bus, ScanState *state)
{
    pci_for_each_device_under_bus(bus, scan_bus_device, state);
}

static void scan_all_host_bridges(FixedBarsInfo *info, Error **errp)
{
    PCIHostState *hb;
    ScanState state = {
        .info = info,
        .is_fixed_subtree = false,
        .errp = errp,
    };

    fixed_claims_init(info);
    QLIST_FOREACH(hb, &pci_host_bridges, next) {
        if (hb->bus) {
            scan_bus(hb->bus, &state);
        }
    }
    fixed_claims_free(info);
}

/*
 * fixed_bars_validate - scan all PCI devices, validate fixed BAR addresses.
 *
 * @info: carries MMIO window bounds for validation.
 * @errp: set on a validation error.
 *
 * Returns true if every BAR validated, false with @errp set otherwise.
 * Whether any fixed BAR device was found is left in info->any_fixed.
 */
bool fixed_bars_validate(FixedBarsInfo *info, Error **errp)
{
    ERRP_GUARD();

    info->any_fixed = false;
    scan_all_host_bridges(info, errp);
    return !*errp;
}

bool fixed_bars_check(uint64_t mmio32_base, uint64_t mmio32_size,
                      uint64_t mmio64_base, uint64_t mmio64_size,
                      Error **errp)
{
    FixedBarsInfo info;

    if (!mmio32_size) {
        error_setg(errp, "fixed-bar: zero-size 32-bit PCIe MMIO aperture");
        return false;
    }
    if (mmio32_size - 1 > UINT64_MAX - mmio32_base ||
        (mmio64_size && mmio64_size - 1 > UINT64_MAX - mmio64_base)) {
        error_setg(errp, "fixed-bar: PCIe MMIO aperture base+size overflows "
                   "(32-bit base=0x%"PRIx64" size=0x%"PRIx64", "
                   "64-bit base=0x%"PRIx64" size=0x%"PRIx64")",
                   mmio32_base, mmio32_size, mmio64_base, mmio64_size);
        return false;
    }

    info.mmio32_base = mmio32_base;
    info.mmio32_limit = mmio32_base + mmio32_size - 1;
    /* An absent 64-bit aperture is an empty window, no BAR fits in it. */
    info.mmio64_base = mmio64_size ? mmio64_base : 1;
    info.mmio64_limit = mmio64_size ? mmio64_base + mmio64_size - 1 : 0;

    return fixed_bars_validate(&info, errp) && info.any_fixed;
}
