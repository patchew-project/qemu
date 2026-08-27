/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * Write validated fixed-BAR device info to the etc/fixed-bars fw_cfg blob.
 *
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "qemu/error-report.h"
#include "pci-internal.h"
#include "pci-fixed-bar.h"
#include "pci-fixed-bar-validate.h"

typedef struct {
    uint8_t  bar;
    uint32_t flags;
    uint64_t addr;
    uint64_t size;
} BarInfo;

/* One PCI function entry to be written to the blob. */
typedef struct {
    uint8_t  rp_bus;     /* primary bus of the root port this device is under */
    uint16_t vendor_id;
    uint16_t device_id;
    bool     is_fixed;
    GArray  *bars;  /* array of BarInfo */
} DeviceInfo;

typedef struct {
    GArray *devices;
    PCIBus *root_bus;
} CollectCtx;

static void collect_device(PCIBus *bus, PCIDevice *pdev, void *opaque);

/* Visit every device on this bus, in devfn order. */
static void collect_bus(PCIBus *bus, void *opaque)
{
    pci_for_each_device_under_bus(bus, collect_device, opaque);
}

/*
 * Primary bus number of the nearest TYPE_PCIE_ROOT_PORT ancestor of bus
 * — i.e. the bus the root port device itself lives on, not its
 * secondary bus. Falls back to the host bridge's own bus number if bus
 * isn't under any root port (e.g. a device directly on the host
 * bridge's primary bus).
 */
static uint8_t find_root_port_bus(PCIBus *bus, PCIBus *host_bus)
{
    PCIDevice *parent;

    while (bus) {
        parent = bus->parent_dev;
        if (!parent) {
            break;
        }
        if (object_dynamic_cast(OBJECT(parent), TYPE_PCIE_ROOT_PORT)) {
            return pci_bus_num(pci_get_bus(parent));
        }
        bus = pci_get_bus(parent);
    }
    return pci_bus_num(host_bus);
}

static void collect_device(PCIBus *bus, PCIDevice *pdev, void *opaque)
{
    CollectCtx *ctx = opaque;
    bool has_mem_bar = false;
    bool is_64bit, is_pref;
    DeviceInfo dev;
    PCIIORegion *r;
    PCIBus *sec;
    BarInfo bi;
    int i;

    for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
        r = &pdev->io_regions[i];
        if (r->size && !(r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
            has_mem_bar = true;
            break;
        }
    }

    /*
     * Every device with a memory BAR gets an entry, fixed or not — not
     * just ones with pci-bars= set. This preserves the exact device
     * sequence CheckDevice() needs for positional VID:DID matching (see
     * collect_all_host_bridges()); only fixed devices carry BAR records
     * (dev.bars stays empty, dev.is_fixed false, otherwise).
     */
    if (has_mem_bar) {
        memset(&dev, 0, sizeof(dev));
        dev.rp_bus    = find_root_port_bus(bus, ctx->root_bus);
        dev.vendor_id = pci_get_word(pdev->config + PCI_VENDOR_ID);
        dev.device_id = pci_get_word(pdev->config + PCI_DEVICE_ID);
        dev.is_fixed  = (pdev->fixed_bar_addrs != NULL);
        dev.bars      = g_array_new(false, true, sizeof(BarInfo));

        if (dev.is_fixed) {
            for (i = 0; i < PCI_NUM_REGIONS - 1; i++) {
                r = &pdev->io_regions[i];
                if (!r->size || (r->type & PCI_BASE_ADDRESS_SPACE_IO)) {
                    continue;
                }
                is_64bit = !!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64);
                is_pref  = !!(r->type & PCI_BASE_ADDRESS_MEM_PREFETCH);

                memset(&bi, 0, sizeof(bi));
                bi.bar   = i;
                bi.addr  = (uint64_t)pdev->fixed_bar_addrs[i];
                bi.size  = r->size;
                bi.flags = (is_64bit ? QEMU_FIXED_BAR_F_MEM64 : 0) |
                           (is_pref  ? QEMU_FIXED_BAR_F_PREF  : 0);
                g_array_append_val(dev.bars, bi);
            }
        }
        g_array_append_val(ctx->devices, dev);
    }

    if (!object_dynamic_cast(OBJECT(pdev), TYPE_PCI_BRIDGE)) {
        return;
    }
    sec = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (sec) {
        collect_bus(sec, opaque);
    }
}

static gint cmp_host_bus_num(gconstpointer a, gconstpointer b)
{
    PCIHostState *ha = *(PCIHostState **)a;
    PCIHostState *hb = *(PCIHostState **)b;
    return (gint)pci_bus_num(ha->bus) - (gint)pci_bus_num(hb->bus);
}

/*
 * EFI_INCOMPATIBLE_PCI_DEVICE_SUPPORT_PROTOCOL's CheckDevice interface
 * (UEFI PI Spec 1.2 Vol 5) only passes VendorId/DeviceId/RevisionId/
 * SubsystemVendorId/SubsystemDeviceId, no BDF — so in EDK2 we can't
 * match a blob entry to a specific device directly using BDF. To work
 * around that, we prepare the blob entries in the same order PciBusDxe
 * discovers devices, so matching by VID:DID inherently works.
 */
static void collect_all_host_bridges(CollectCtx *ctx)
{
    PCIHostState *hb;
    GPtrArray *sorted;
    guint i;

    sorted = g_ptr_array_new();
    QLIST_FOREACH(hb, &pci_host_bridges, next) {
        if (hb->bus) {
            g_ptr_array_add(sorted, hb);
        }
    }
    g_ptr_array_sort(sorted, cmp_host_bus_num);

    for (i = 0; i < sorted->len; i++) {
        hb = g_ptr_array_index(sorted, i);
        ctx->root_bus = hb->bus;
        collect_bus(hb->bus, ctx);
    }

    g_ptr_array_free(sorted, true);
}

static uint8_t *create_blob(CollectCtx *ctx, size_t *blob_size)
{
    QemuFixedBarsDevice *dout;
    QemuFixedBarsBar *bout;
    QemuFixedBarsHdr *hdr;
    DeviceInfo *dev;
    uint8_t *blob;
    uint8_t *ptr;
    BarInfo *bi;
    size_t sz;
    guint d, b;

    sz = sizeof(QemuFixedBarsHdr);
    for (d = 0; d < ctx->devices->len; d++) {
        dev = &g_array_index(ctx->devices, DeviceInfo, d);
        sz += sizeof(QemuFixedBarsDevice);
        sz += dev->bars->len * sizeof(QemuFixedBarsBar);
    }

    blob = g_malloc0(sz);
    hdr  = (QemuFixedBarsHdr *)blob;
    hdr->version     = cpu_to_le32(QEMU_FIXED_BARS_VERSION);
    hdr->num_devices = cpu_to_le32(ctx->devices->len);

    ptr = blob + sizeof(*hdr);
    for (d = 0; d < ctx->devices->len; d++) {
        dev  = &g_array_index(ctx->devices, DeviceInfo, d);
        dout = (QemuFixedBarsDevice *)ptr;

        dout->vendor_id = cpu_to_le16(dev->vendor_id);
        dout->device_id = cpu_to_le16(dev->device_id);
        dout->dev_flags = dev->is_fixed ? QEMU_FIXED_BARS_DEV_F_FIXED : 0;
        dout->rp_bus    = dev->rp_bus;
        dout->num_bars  = dev->bars->len;
        dout->reserved  = 0;
        ptr += sizeof(QemuFixedBarsDevice);

        for (b = 0; b < dev->bars->len; b++) {
            bi   = &g_array_index(dev->bars, BarInfo, b);
            bout = (QemuFixedBarsBar *)ptr;

            bout->bar     = bi->bar;
            bout->flags   = cpu_to_le32(bi->flags);
            bout->address = cpu_to_le64(bi->addr);
            bout->size    = cpu_to_le64(bi->size);
            ptr += sizeof(QemuFixedBarsBar);
        }
    }

    *blob_size = sz;
    return blob;
}

/*
 * fixed_bars_write_blob - validate fixed BAR addresses and write fw_cfg blob.
 *
 * @fw_cfg:       fw_cfg state to write the etc/fixed-bars blob into.
 * @mmio32_base:  base of the machine's 32-bit PCIe MMIO aperture.
 * @mmio32_size:  size of the machine's 32-bit PCIe MMIO aperture.
 * @mmio64_base:  base of the machine's 64-bit PCIe MMIO aperture.
 * @mmio64_size:  size of the machine's 64-bit PCIe MMIO aperture.
 *
 * Returns true if any fixed BAR device was found and the blob was
 * written, false if there was nothing to do. Aborts if any BAR
 * address fails validation.
 */
bool fixed_bars_write_blob(FWCfgState *fw_cfg,
                           uint64_t mmio32_base,
                           uint64_t mmio32_size,
                           uint64_t mmio64_base,
                           uint64_t mmio64_size)
{
    FixedBarsInfo info;
    CollectCtx ctx;
    uint8_t *blob;
    size_t blob_size;
    guint d;

    if (!mmio32_size || !mmio64_size) {
        error_report("pci-bars: zero-size PCIe MMIO aperture "
                     "(32-bit=0x%"PRIx64", 64-bit=0x%"PRIx64")",
                     mmio32_size, mmio64_size);
        exit(1);
    }
    if (mmio32_size - 1 > UINT64_MAX - mmio32_base ||
        mmio64_size - 1 > UINT64_MAX - mmio64_base) {
        error_report("pci-bars: PCIe MMIO aperture base+size overflows "
                     "(32-bit base=0x%"PRIx64" size=0x%"PRIx64", "
                     "64-bit base=0x%"PRIx64" size=0x%"PRIx64")",
                     mmio32_base, mmio32_size, mmio64_base, mmio64_size);
        exit(1);
    }

    info.mmio32_base = mmio32_base;
    info.mmio32_limit = mmio32_base + mmio32_size - 1;
    info.mmio64_base = mmio64_base;
    info.mmio64_limit = mmio64_base + mmio64_size - 1;

    if (!fixed_bars_validate(&info)) {
        return false;
    }

    ctx.devices  = g_array_new(false, true, sizeof(DeviceInfo));
    ctx.root_bus = NULL;
    collect_all_host_bridges(&ctx);

    blob = create_blob(&ctx, &blob_size);
    fw_cfg_add_file(fw_cfg, FW_CFG_FIXED_BARS, blob, blob_size);

    for (d = 0; d < ctx.devices->len; d++) {
        g_array_free(g_array_index(ctx.devices, DeviceInfo, d).bars, true);
    }
    g_array_free(ctx.devices, true);

    return true;
}
