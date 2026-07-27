/*
 * QEMU Intel 82576 SR/IOV VF Migration Support
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef IGB_MIGRATION_H
#define IGB_MIGRATION_H

#include "hw/pci/pci_device.h"

/* Migration BAR definitions */
#define IGB_MIG_BAR_IDX   (2)
#define IGB_MIG_BAR_SIZE  (64 * 1024)

/*
 * Vendor-specific PCI capability for migration discovery.
 *
 * The igb-vfio-pci variant driver probes for this at bind time.
 * If present, the driver knows the emulated VF supports migration.
 */
#define IGB_MIG_CAP_MAGIC    0x4D494742  /* "MIGB" */
#define IGB_MIG_CAP_VERSION  1

#define IGB_MIG_CAP_F_STATE  (1u << 0)   /* device state serialization */
#define IGB_MIG_CAP_F_DIRTY  (1u << 1)   /* dirty page tracking */

#define IGB_MIG_CAP_SIZE     16
#define IGB_MIG_CAP_OFF_MAGIC  4    /* offset within cap for magic field */
#define IGB_MIG_CAP_OFF_BARID  8    /* offset within cap for BAR id */
#define IGB_MIG_CAP_OFF_FLAGS  12   /* offset within cap for feature flags */

typedef struct IgbVfMigState {
    bool migration_cap;
    MemoryRegion mig_bar;
} IgbVfMigState;

void igb_pf_init_migration_bar(PCIDevice *dev);
bool igbvf_add_migration_cap(PCIDevice *dev, Error **errp);

#endif
