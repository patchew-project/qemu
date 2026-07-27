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

/* MIG_CAPS register layout (read-only, offset 0x008) */
#define IGB_MIG_CAPS_MAX_RANGES_SHIFT  8
#define IGB_MIG_CAPS_MAX_RANGES_MASK   (0xfu << 8)   /* bits [11:8] */
#define IGB_MIG_CAPS_MAX_RANGES        4
#define IGB_MIG_CAPS_PGSIZES_MASK      0xfffff000u    /* bits [31:12] */

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
#define IGB_MIG_DIRTY_PGSIZE        0x020
#define IGB_MIG_DIRTY_CTRL          0x024
#define IGB_MIG_DIRTY_RANGE_IOVA_LO 0x028
#define IGB_MIG_DIRTY_RANGE_IOVA_HI 0x02C
#define IGB_MIG_DIRTY_RANGE_SIZE    0x030
#define IGB_MIG_DIRTY_BUF_ADDR_LO   0x034
#define IGB_MIG_DIRTY_BUF_ADDR_HI   0x038
#define IGB_MIG_DIRTY_STATUS        0x03C

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

/*
 * DIRTY_CTRL register values. Write one of these to control
 * the dirty page tracking state machine.
 */
#define IGB_MIG_DIRTY_CTRL_DISABLE   0
#define IGB_MIG_DIRTY_CTRL_ENABLE    1
#define IGB_MIG_DIRTY_CTRL_QUERY     2  /* query-and-clear */

/* DIRTY_STATUS register values (read-only, cleared on next DIRTY_CTRL write) */
#define IGB_MIG_DIRTY_STATUS_OK              0
#define IGB_MIG_DIRTY_STATUS_TOO_MANY_RANGES 1
#define IGB_MIG_DIRTY_STATUS_BAD_RANGE       2
#define IGB_MIG_DIRTY_STATUS_BAD_PGSIZE      3
#define IGB_MIG_DIRTY_STATUS_NOT_ENABLED     4
#define IGB_MIG_DIRTY_STATUS_NO_BUFFER       5

#define IGB_MIG_DIRTY_DEFAULT_PGSIZE  4096

/*
 * Dirty query shared buffer layout - DMA between driver and device.
 * The driver writes request fields, kicks DIRTY_CTRL=QUERY, and the
 * device reads the request, fills bitmap + completion via DMA. Each
 * section is cache-line aligned (64 bytes).
 */
struct igb_mig_dirty_query {
    /* Cache line 0: request (written by driver) */
    uint64_t iova;
    uint64_t size;
    uint32_t page_size;
    uint32_t flags;
    uint32_t reserved0[10];

    /* Cache line 1: completion (written by device) */
    uint32_t status;
    uint32_t bitmap_size;
    uint32_t dirty_page_count;
    uint32_t reserved1[12];

    /* Cache line 2+: bitmap (written by device) */
    uint8_t bitmap[];
};

#define IGB_MIG_DIRTY_STATUS_COMPLETE  1

typedef struct IgbVfMigState {
    bool migration_cap;
    MemoryRegion mig_bar;

    uint32_t mig_state;
    uint8_t mig_error;
    uint8_t mig_data[IGB_VF_STATE_MAX_SIZE];
    uint32_t mig_data_size;
    uint64_t mig_data_buf_addr;

    uint32_t mig_dirty_pgsize;
    uint64_t mig_dirty_range_iova;
    uint32_t mig_dirty_range_size;
    uint64_t mig_dirty_buf_addr;
    uint32_t mig_dirty_status;
} IgbVfMigState;

typedef struct IgbVfState  IgbVfState;
void igb_pf_init_migration_bar(PCIDevice *dev);
bool igbvf_add_migration_cap(PCIDevice *dev, Error **errp);
void igbvf_mig_bar_init(IgbVfState *s);
void igbvf_mig_state_reset(IgbVfState *s);

typedef struct IGBVfDirtyRange {
    uint64_t iova;
    uint64_t size;
    uint64_t page_size;
    unsigned long *bitmap;
    uint64_t nbits;
} IGBVfDirtyRange;

typedef struct IGBVfDirtyState {
    uint32_t num_ranges;
    IGBVfDirtyRange ranges[IGB_MIG_CAPS_MAX_RANGES];
} IGBVfDirtyState;

typedef struct IGBCore IGBCore;
void igb_core_dirty_track_dma(IGBCore *core, int vfn,
                              dma_addr_t addr, dma_addr_t len);
void igb_core_vf_dirty_disable(IgbVfState *s);

#endif
