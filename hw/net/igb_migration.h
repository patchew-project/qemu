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

/*
 * Maximum serialized VF state size, sized to hold all per-VF
 * registers plus TX context descriptors with room to spare.
 */
#define IGB_VF_STATE_MAX_SIZE   4096

/*
 * Migration BAR register offsets.
 */
#define IGB_MIG_DEVICE_STATE        0x000
#define IGB_MIG_STATUS              0x004
#define IGB_MIG_CAPS                0x008
#define IGB_MIG_VERSION             0x00C
#define IGB_MIG_DATA_SIZE           0x010
#define IGB_MIG_DATA_XFER           0x014
#define IGB_MIG_DATA_BUF_ADDR_LO    0x018
#define IGB_MIG_DATA_BUF_ADDR_HI    0x01C

/* DEVICE_STATE values - mirrors VFIO migration states */
#define IGB_MIG_STATE_ERROR         0
#define IGB_MIG_STATE_STOP          1
#define IGB_MIG_STATE_RUNNING       2
#define IGB_MIG_STATE_STOP_COPY     3
#define IGB_MIG_STATE_RESUMING      4
#define IGB_MIG_STATE_PRE_COPY      5

/* MIG_STATUS bits */
#define IGB_MIG_STATUS_DATA_AVAIL   (1u << 0)
#define IGB_MIG_STATUS_ERROR        (1u << 1)

/* MIG_STATUS error codes in bits [15:8], valid when ERROR bit is set */
#define IGB_MIG_STATUS_ERR_SHIFT    8
#define IGB_MIG_STATUS_ERR_MASK     (0xffu << IGB_MIG_STATUS_ERR_SHIFT)
#define IGB_MIG_STATUS_ERR(code)    (IGB_MIG_STATUS_ERROR | \
                                     ((uint32_t)(code) << IGB_MIG_STATUS_ERR_SHIFT))

#define IGB_MIG_ERR_BAD_MAGIC       1
#define IGB_MIG_ERR_BAD_VERSION     2
#define IGB_MIG_ERR_BAD_SIZE        3
#define IGB_MIG_ERR_BAD_VFN         4
#define IGB_MIG_ERR_DMA_FAILED      5
#define IGB_MIG_ERR_NO_BUFFER       6

typedef struct IgbVfMigState {
    bool migration_cap;
    MemoryRegion mig_bar;

    uint32_t mig_state;
    uint8_t mig_error;
    uint8_t mig_data[IGB_VF_STATE_MAX_SIZE];
    uint32_t mig_data_size;
    uint64_t mig_data_buf_addr;
} IgbVfMigState;

typedef struct IgbVfState  IgbVfState;
void igb_pf_init_migration_bar(PCIDevice *dev);
bool igbvf_add_migration_cap(PCIDevice *dev, Error **errp);
void igbvf_mig_bar_init(IgbVfState *s);
void igbvf_mig_state_reset(IgbVfState *s);

#endif
