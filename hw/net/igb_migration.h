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
 *   +0x0C  CAPS                       (RO: F_STATE[0])
 *   +0x10  CTRL                       (WO: doorbell command)
 *   +0x14  STATUS                     (RO: state[7:0], error_code[15:8])
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

/* CTRL register: command in [7:0] */
#define IGB_MIG_CTRL_CMD_MASK           0xFF
#define IGB_MIG_CTRL_ARG_SHIFT          8

/* CTRL commands */
#define IGB_MIG_CMD_SET_STATE           1
#define IGB_MIG_CMD_SAVE                2
#define IGB_MIG_CMD_LOAD                3

/* STATUS register: state in [7:0], error code [15:8] */
#define IGB_MIG_STATUS_STATE_MASK       0xFF
#define IGB_MIG_STATUS_ERROR_CODE_SHIFT 8
#define IGB_MIG_STATUS_ERR(code) \
    ((uint32_t)(code) << IGB_MIG_STATUS_ERROR_CODE_SHIFT)

/* Device states (based on VFIO migration v2) */
#define IGB_MIG_STATE_ERROR             0
#define IGB_MIG_STATE_STOP              1
#define IGB_MIG_STATE_RUNNING           2
#define IGB_MIG_STATE_STOP_COPY         3
#define IGB_MIG_STATE_RESUMING          4

/* Error codes */
#define IGB_MIG_ERR_UNK_CMD             1
#define IGB_MIG_ERR_BAD_STATE           2
#define IGB_MIG_ERR_NO_BUFFER           3
#define IGB_MIG_ERR_DMA_FAILED          4
#define IGB_MIG_ERR_BAD_SIZE            5
#define IGB_MIG_ERR_BAD_MAGIC           6
#define IGB_MIG_ERR_BAD_VERSION         7

/* Shared buffer constants */
#define IGB_VF_STATE_MAX_SIZE           4096

typedef struct IgbVfMigState {
    uint32_t mig_state;
    uint32_t mig_data[IGB_VF_STATE_MAX_SIZE / sizeof(uint32_t)];
    uint32_t mig_data_size;
    uint64_t mig_data_buf_addr;
} IgbVfMigState;

typedef struct IgbVfState IgbVfState;

bool igbvf_add_migration_dvsec(PCIDevice *dev, Error **errp);
void igbvf_mig_state_reset(IgbVfState *s);
uint32_t igbvf_mig_config_read(IgbVfState *s, uint32_t addr, int size);
bool igbvf_mig_config_write(IgbVfState *s, uint32_t addr, uint32_t val,
                            int size);

#endif
