/*
 * Cluster Power Controller emulation
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * Copyright (c) 2025 MIPS
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#ifndef RISCV_CPC_H
#define RISCV_CPC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define CPC_ADDRSPACE_SZ    0x6000

/* CPC global register offsets relative to base address */
#define CPC_MTIME_REG_OFS   0x50

#define CPC_CM_STAT_CONF_OFS   0x1008

/* CPC blocks offsets relative to base address */
#define CPC_CL_BASE_OFS     0x2000
#define CPC_CORE_REG_STRIDE 0x100 /* Stride between core-specific registers */

/* CPC register offsets relative to block offsets */
#define CPC_STAT_CONF_OFS   0x08
#define CPC_VP_STOP_OFS     0x20
#define CPC_VP_RUN_OFS      0x28
#define CPC_VP_RUNNING_OFS  0x30

#define SEQ_STATE_BIT       19
#define SEQ_STATE_U5        0x6
#define SEQ_STATE_U6        0x7
#define CPC_Cx_STAT_CONF_SEQ_STATE_U5      (SEQ_STATE_U5 << SEQ_STATE_BIT)
#define CPC_Cx_STAT_CONF_SEQ_STATE_U6      (SEQ_STATE_U6 << SEQ_STATE_BIT)

#define TYPE_RISCV_CPC "xmips-cpc"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVCPCState, RISCV_CPC)

typedef struct RISCVCPCState {
    SysBusDevice parent_obj;

    uint32_t cluster_id;
    uint32_t num_vp;
    uint32_t num_hart;
    uint32_t num_core;
    uint64_t vps_start_running_mask; /* VPs running from restart mask */

    MemoryRegion mr;
    uint64_t vps_running_mask; /* Indicates which VPs are in the run state mask */

    /* Array of CPUs managed by this CPC */
    CPUState **cpus;
} RISCVCPCState;

#define CPC_MAX_VPS 64  /* Maximum number of VPs supported */

#endif /* RISCV_CPC_H */
