/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Imagination Technologies
 *
 * Copyright (C) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef RISCV_CMGCR_H
#define RISCV_CMGCR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RISCV_GCR "riscv-gcr"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVGCRState, RISCV_GCR)

#define GCR_BASE_ADDR           0x1fb80000ULL
#define GCR_ADDRSPACE_SZ        0x8000

/* Offsets to register blocks */
#define RISCV_GCB_OFS        0x0000 /* Global Control Block */
#define RISCV_CLCB_OFS       0x2000 /* Core Control Block */

/* Global Control Block Register Map */
#define GCR_CONFIG_OFS      0x0000
#define GCR_BASE_OFS        0x0008
#define GCR_REV_OFS         0x0030
#define GCR_CPC_STATUS_OFS  0x00F0
#define GCR_L2_CONFIG_OFS   0x0130

/* GCR_L2_CONFIG register fields */
#define GCR_L2_CONFIG_BYPASS_SHF    20
#define GCR_L2_CONFIG_BYPASS_MSK    ((0x1ULL) << GCR_L2_CONFIG_BYPASS_SHF)

/* GCR_BASE register fields */
#define GCR_BASE_GCRBASE_MSK     0xffffffff8000ULL

/* GCR_CPC_BASE register fields */
#define GCR_CPC_BASE_CPCEN_MSK   1
#define GCR_CPC_BASE_CPCBASE_MSK 0xFFFFFFFF8000ULL
#define GCR_CPC_BASE_MSK (GCR_CPC_BASE_CPCEN_MSK | GCR_CPC_BASE_CPCBASE_MSK)

/* GCR_CL_RESETBASE_OFS register fields */
#define GCR_CL_RESET_BASE_RESETBASE_MSK 0xFFFFFFFFFFFFF000U
#define GCR_CL_RESET_BASE_MSK GCR_CL_RESET_BASE_RESETBASE_MSK

typedef struct RISCVGCRVPState RISCVGCRVPState;
struct RISCVGCRVPState {
    uint64_t reset_base;
};

typedef struct RISCVGCRState RISCVGCRState;
struct RISCVGCRState {
    SysBusDevice parent_obj;

    int32_t gcr_rev;
    uint32_t cluster_id;
    uint32_t num_vps;
    uint32_t num_hart;
    uint32_t num_core;
    hwaddr gcr_base;
    MemoryRegion iomem;
    MemoryRegion *cpc_mr;

    uint64_t cpc_base;

    /* VP Local/Other Registers */
    RISCVGCRVPState *vps;
};

#endif /* RISCV_CMGCR_H */
