/*
 * QEMU Intel 82576 SR/IOV VF Migration Support
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_IGB_MIGRATION_H
#define HW_NET_IGB_MIGRATION_H

#include "hw/pci/pci_device.h"

/*
 * Migration interface exposed as a DVSEC (Designated Vendor-Specific
 * Extended Capability) in VF extended config space.
 *
 * DVSEC layout at IGB_MIG_DVSEC_OFFSET (0x160):
 *
 *   +0x00  PCIe extended cap header   (cap_id=0x23, ver=1, next)
 *   +0x04  DVSEC header 1             (len | rev | vendor_id)
 *   +0x08  DVSEC header 2             (DVSEC ID)
 *   +0x0A  Reserved                   (padding for DWORD alignment)
 *   +0x0C  CAPS                       (RO: F_STATE[0], F_DIRTY[1],
 *                                      max_ranges[11:8], pgsize[16:12])
 *   +0x10  CTRL                       (WO: doorbell command)
 *   +0x14  STATUS                     (RO: state[7:0], error_code[15:8],
 *                                      QUIESCED[16])
 *   +0x18  BUF_ADDR_LO                (RW: shared buffer GPA low)
 *   +0x1C  BUF_ADDR_HI                (RW: shared buffer GPA high)
 *   +0x20  DATA_SIZE                  (RO: max state blob size in bytes)
 */

#define IGB_MIG_DVSEC_OFFSET    0x160
#define IGB_MIG_DVSEC_SIZE      0x24
#define IGB_MIG_DVSEC_VER       1
#define IGB_MIG_DVSEC_ID        1

/* Register offsets relative to DVSEC base */
#define IGB_MIG_CAPS            0x0C
#define IGB_MIG_CTRL            0x10
#define IGB_MIG_STATUS          0x14
#define IGB_MIG_BUF_ADDR_LO     0x18
#define IGB_MIG_BUF_ADDR_HI     0x1C
#define IGB_MIG_DATA_SIZE       0x20

/* CAPS register layout */
#define IGB_MIG_CAP_F_STATE             (1u << 0)
#define IGB_MIG_CAP_F_DIRTY             (1u << 1)
#define IGB_MIG_CAPS_MAX_RANGES_SHIFT   8
#define IGB_MIG_CAPS_MAX_RANGES         4
#define IGB_MIG_CAPS_PGSIZE_SHIFT       12
#define IGB_MIG_CAPS_PGSIZE_4K          (1u << 12)
#define IGB_MIG_CAPS_PGSIZE_64K         (1u << 16)

/* CTRL register: command in [7:0] */
#define IGB_MIG_CTRL_CMD_MASK           0xFF
#define IGB_MIG_CTRL_ARG_SHIFT          8

/* CTRL commands */
#define IGB_MIG_CMD_SET_STATE           1
#define IGB_MIG_CMD_SAVE                2
#define IGB_MIG_CMD_LOAD                3
#define IGB_MIG_CMD_DIRTY_ENABLE        4
#define IGB_MIG_CMD_DIRTY_DISABLE       5
#define IGB_MIG_CMD_DIRTY_QUERY         6
#define IGB_MIG_CMD_GET_STATS           7

/* STATUS register: state in [7:0], error code [15:8] */
#define IGB_MIG_STATUS_STATE_MASK       0xFF
#define IGB_MIG_STATUS_ERROR_CODE_SHIFT 8
#define IGB_MIG_STATUS_ERR(code) \
    ((uint32_t)(code) << IGB_MIG_STATUS_ERROR_CODE_SHIFT)
#define IGB_MIG_STATUS_QUIESCED         (1u << 16)

/* Device states (based on VFIO migration v2) */
#define IGB_MIG_STATE_ERROR             0
#define IGB_MIG_STATE_STOP              1
#define IGB_MIG_STATE_RUNNING           2
#define IGB_MIG_STATE_STOP_COPY         3
#define IGB_MIG_STATE_RESUMING          4
#define IGB_MIG_STATE_PRE_COPY          5

/* Error codes */
#define IGB_MIG_ERR_UNK_CMD             1
#define IGB_MIG_ERR_BAD_STATE           2
#define IGB_MIG_ERR_NO_BUFFER           3
#define IGB_MIG_ERR_DMA_FAILED          4
#define IGB_MIG_ERR_BAD_SIZE            5
#define IGB_MIG_ERR_BAD_MAGIC           6
#define IGB_MIG_ERR_BAD_VERSION         7
#define IGB_MIG_ERR_TOO_MANY_RANGES     8
#define IGB_MIG_ERR_BAD_RANGE           9
#define IGB_MIG_ERR_BAD_PGSIZE          10
#define IGB_MIG_ERR_NOT_ENABLED         11

/* Shared buffer constants */
#define IGB_VF_STATE_MAX_SIZE           4096

#define IGB_MIG_DIRTY_DEFAULT_PGSIZE        4096

typedef struct IGBVfDirtyRange {
    uint64_t iova;
    uint64_t size;
    uint64_t page_size;
    unsigned long *bitmap;
    uint64_t nbits;
} IGBVfDirtyRange;

typedef struct IGBVfDirtyState {
    IGBVfDirtyRange ranges[IGB_MIG_CAPS_MAX_RANGES];
    uint32_t num_ranges;
} IGBVfDirtyState;

typedef struct IgbVfMigStats {
    uint64_t dma_writes;
    uint64_t dma_bytes;
    uint32_t dirty_pages_set;
    uint32_t dirty_pages_cleared;
    uint32_t dirty_page_count;
    uint32_t dirty_query_count;
} IgbVfMigStats;

typedef struct IgbVfMigState {
    uint32_t mig_state;
    uint32_t mig_data[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];
    uint32_t mig_data_size;
    uint64_t mig_data_buf_addr;
    bool mig_saved_vfre;
    bool mig_saved_vfte;
} IgbVfMigState;

/*
 * DMA buffer layouts for dirty tracking commands.
 *
 * DIRTY_ENABLE: driver writes igb_mig_dirty_enable_req to buffer
 *               before cmd.
 * DIRTY_QUERY: driver writes iova/size fields, device writes
 *              response + bitmap.
 */
struct igb_mig_dirty_enable_req {
    uint32_t len;
    uint32_t flags;
    uint64_t pgsize;
    uint64_t range_iova;
    uint64_t range_size;
    uint32_t reserved[4];
};

struct igb_mig_dirty_query {
    uint32_t len;
    uint32_t flags;
    uint64_t iova;
    uint64_t size;
    uint32_t bitmap_size;
    uint32_t dirty_page_count;
    uint64_t dma_writes;
    uint32_t reserved[6];
    uint8_t bitmap[];
};

/*
 * GET_STATS:    device writes igb_mig_stats_resp to buffer.
 */
struct igb_mig_stats_resp {
    uint64_t dma_writes;
    uint64_t dma_bytes;
    uint32_t dirty_pages_set;
    uint32_t dirty_pages_cleared;
    uint32_t dirty_page_count;
    uint32_t dirty_query_count;
};

typedef struct IGBCore IGBCore;
typedef struct IgbVfState IgbVfState;

bool igbvf_add_migration_dvsec(PCIDevice *dev, Error **errp);
void igbvf_mig_state_reset(IgbVfState *s);
uint32_t igbvf_mig_config_read(IgbVfState *s, uint32_t addr, int size);
bool igbvf_mig_config_write(IgbVfState *s, uint32_t addr, uint32_t val,
                            int size);

void igb_core_dirty_track_dma(IGBCore *core, int vfn,
                              dma_addr_t addr, dma_addr_t len);

#endif
