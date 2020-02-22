/*
 * QEMU abstraction of Host IOMMU
 *
 * Copyright (C) 2020 Intel Corporation.
 *
 * Authors: Liu Yi L <yi.l.liu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_IOMMU_CONTEXT_H
#define HW_IOMMU_CONTEXT_H

#include "qemu/queue.h"
#include <linux/iommu.h>
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

typedef struct HostIOMMUContext HostIOMMUContext;
typedef struct HostIOMMUOps HostIOMMUOps;
typedef struct HostIOMMUInfo HostIOMMUInfo;
typedef struct DualIOMMUStage1BindData DualIOMMUStage1BindData;

struct HostIOMMUOps {
    /* Allocate pasid from HostIOMMUContext (a.k.a. host software) */
    int (*pasid_alloc)(HostIOMMUContext *host_icx,
                       uint32_t min,
                       uint32_t max,
                       uint32_t *pasid);
    /* Reclaim pasid from HostIOMMUContext (a.k.a. host software) */
    int (*pasid_free)(HostIOMMUContext *host_icx,
                      uint32_t pasid);
    /*
     * Bind stage-1 page table to a hostIOMMU w/ dual stage
     * DMA translation capability.
     * @bind_data specifies the bind configurations.
     */
    int (*bind_stage1_pgtbl)(HostIOMMUContext *dsi_obj,
                             DualIOMMUStage1BindData *bind_data);
    /* Undo a previous bind. @bind_data specifies the unbind info. */
    int (*unbind_stage1_pgtbl)(HostIOMMUContext *dsi_obj,
                               DualIOMMUStage1BindData *bind_data);
};

struct HostIOMMUInfo {
    uint32_t stage1_format;
};

/*
 * This is an abstraction of host IOMMU with dual-stage capability
 */
struct HostIOMMUContext {
#define HOST_IOMMU_PASID_REQUEST (1ULL << 0)
#define HOST_IOMMU_NESTING       (1ULL << 1)
    uint64_t flags;
    HostIOMMUOps *ops;
    HostIOMMUInfo uinfo;
};

struct DualIOMMUStage1BindData {
    uint32_t pasid;
    union {
        struct iommu_gpasid_bind_data gpasid_bind;
    } bind_data;
};

int host_iommu_ctx_pasid_alloc(HostIOMMUContext *host_icx, uint32_t min,
                               uint32_t max, uint32_t *pasid);
int host_iommu_ctx_pasid_free(HostIOMMUContext *host_icx, uint32_t pasid);
int host_iommu_ctx_bind_stage1_pgtbl(HostIOMMUContext *host_icx,
                                     DualIOMMUStage1BindData *data);
int host_iommu_ctx_unbind_stage1_pgtbl(HostIOMMUContext *host_icx,
                                       DualIOMMUStage1BindData *data);

void host_iommu_ctx_init(HostIOMMUContext *host_icx,
                         uint64_t flags, HostIOMMUOps *ops,
                         HostIOMMUInfo *uinfo);
void host_iommu_ctx_destroy(HostIOMMUContext *host_icx);

#endif
