/*
 * QEMU Intel 82576 SR/IOV VF Migration Support
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "igb_common.h"
#include "igb_migration.h"
#include "trace.h"



/*
 * 32-bit prefetchable BAR. A 64-bit BAR2 would consume BAR2+BAR3, but
 * BAR3 is already used for MSI-X (IGBVF_MSIX_BAR_IDX = 3).
 *
 *   BAR   Index       Type            Size   Purpose
 *   BAR0  0+1    64-bit prefetchable  16 KB  MMIO registers
 *   BAR2  2      32-bit prefetchable  64 KB  Migration
 *   BAR3  3+4    64-bit prefetchable  16 KB  MSI-X table + PBA
 *   BAR5  5      -                    -      Unused
 */
void igb_pf_init_migration_bar(PCIDevice *dev)
{
    pcie_sriov_pf_init_vf_bar(dev, IGB_MIG_BAR_IDX,
                              PCI_BASE_ADDRESS_MEM_PREFETCH,
                              IGB_MIG_BAR_SIZE);
}

/*
 * Add vendor-specific PCI capability that the variant driver probes for.
 *
 * Layout (16 bytes):
 *   [0]  cap_id      (PCI_CAP_ID_VNDR = 0x09)
 *   [1]  next_cap
 *   [2]  cap_len     (16)
 *   [3]  version     (IGB_MIG_CAP_VERSION)
 *   [4-7]  magic     (IGB_MIG_CAP_MAGIC, little-endian)
 *   [8-11] bar_id    (IGB_MIG_BAR_IDX, little-endian)
 *   [12-15] flags    (feature flags, little-endian)
 */
bool igbvf_add_migration_cap(PCIDevice *dev, Error **errp)
{
    int offset;

    offset = pci_add_capability(dev, PCI_CAP_ID_VNDR, 0,
                                IGB_MIG_CAP_SIZE, errp);
    if (offset < 0) {
        return false;
    }

    /* Length and version in the standard cap flags word */
    pci_set_byte(dev->config + offset + PCI_CAP_FLAGS,
                 IGB_MIG_CAP_SIZE);
    pci_set_byte(dev->config + offset + PCI_CAP_FLAGS + 1,
                 IGB_MIG_CAP_VERSION);

    pci_set_long(dev->config + offset + IGB_MIG_CAP_OFF_MAGIC,
                 IGB_MIG_CAP_MAGIC);
    pci_set_long(dev->config + offset + IGB_MIG_CAP_OFF_BARID,
                 IGB_MIG_BAR_IDX);
    pci_set_long(dev->config + offset + IGB_MIG_CAP_OFF_FLAGS,
                 IGB_MIG_CAP_F_STATE);

    trace_igbvf_mig_cap_add(pcie_sriov_vf_number(dev), offset);
    return true;
}
