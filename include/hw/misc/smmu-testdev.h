/*
 * A test device for the SMMU
 *
 * This test device is a minimal SMMU-aware device used to test the SMMU.
 *
 * Copyright (c) 2025 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_SMMU_TESTDEV_H
#define HW_MISC_SMMU_TESTDEV_H

#include "qemu/osdep.h"
typedef enum SMMUTestDevSpace {
    STD_SPACE_SECURE    = 0,
    STD_SPACE_NONSECURE = 1,
    STD_SPACE_ROOT      = 2,
    STD_SPACE_REALM     = 3,
} SMMUTestDevSpace;

/* Only the Non-Secure space is implemented; leave room for future domains. */
#define STD_SUPPORTED_SPACES 1

/* BAR0 registers (offsets) */
enum {
    STD_REG_ID           = 0x00,
    STD_REG_ATTR_NS      = 0x04,
    STD_REG_SMMU_BASE_LO = 0x20,
    STD_REG_SMMU_BASE_HI = 0x24,
    STD_REG_DMA_IOVA_LO  = 0x28,
    STD_REG_DMA_IOVA_HI  = 0x2C,
    STD_REG_DMA_LEN      = 0x30,
    STD_REG_DMA_DIR      = 0x34,
    STD_REG_DMA_RESULT   = 0x38,
    STD_REG_DMA_DBELL    = 0x3C,
    /* Extended controls for DMA attributes/mode */
    STD_REG_DMA_MODE     = 0x40,
    STD_REG_DMA_ATTRS    = 0x44,
    /* Translation controls */
    STD_REG_TRANS_MODE   = 0x48,
    STD_REG_S1_SPACE     = 0x4C,
    STD_REG_S2_SPACE     = 0x50,
    STD_REG_TRANS_DBELL  = 0x54,
    STD_REG_TRANS_STATUS = 0x58,
    /* Clear helper-built tables/descriptors (write-any to trigger) */
    STD_REG_TRANS_CLEAR  = 0x5C,
};

/* DMA result/status values shared with tests */
#define STD_DMA_RESULT_IDLE 0xffffffffu
#define STD_DMA_RESULT_BUSY 0xfffffffeu
#define STD_DMA_ERR_BAD_LEN 0xdead0001u
#define STD_DMA_ERR_TX_FAIL 0xdead0002u

/* DMA attributes layout (for STD_REG_DMA_ATTRS) */
#define STD_DMA_ATTR_SECURE        (1u << 0)
#define STD_DMA_ATTR_SPACE_SHIFT   1
#define STD_DMA_ATTR_SPACE_MASK    (0x3u << STD_DMA_ATTR_SPACE_SHIFT)
#define STD_DMA_ATTR_UNSPECIFIED   (1u << 3)

/* Device identity value returned by STD_REG_ID */
#define STD_ID_VALUE 0x53544d4dU /* 'STMM' */

/* Command type */
#define STD_CMD_CFGI_STE        0x03
#define STD_CMD_CFGI_CD         0x05
#define STD_CMD_TLBI_NSNH_ALL   0x30

/*
 * Address-space base offsets for test tables.
 * - Secure uses 0 offset.
 * - Non-Secure uses a fixed offset, keeping internal layout identical.
 *
 * Note: Future spaces (e.g. Realm/Root) are not implemented here.
 * When needed, introduce new offsets and reuse the helpers below so
 * relative layout stays identical across spaces.
 */
#define STD_SPACE_OFFS_NS       0x40000000ULL

static inline uint64_t std_space_offset(SMMUTestDevSpace sp)
{
    /* Non-Secure is the only supported space today; return zero for others. */
    return (sp == STD_SPACE_NONSECURE) ? STD_SPACE_OFFS_NS : 0;
}

#endif /* HW_MISC_SMMU_TESTDEV_H */
