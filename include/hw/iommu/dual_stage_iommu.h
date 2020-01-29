/*
 * QEMU abstraction of IOMMU Context
 *
 * Copyright (C) 2020 Red Hat Inc.
 *
 * Authors: Liu, Yi L <yi.l.liu@intel.com>
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

#ifndef HW_DS_IOMMU_H
#define HW_DS_IOMMU_H

#include "qemu/queue.h"
#include <linux/iommu.h>
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

typedef struct DualStageIOMMUObject DualStageIOMMUObject;
typedef struct DualStageIOMMUOps DualStageIOMMUOps;
typedef struct DualStageIOMMUInfo DualStageIOMMUInfo;
typedef struct DualIOMMUStage1BindData DualIOMMUStage1BindData;
typedef struct DualIOMMUStage1Cache DualIOMMUStage1Cache;

struct DualStageIOMMUOps {
    /* Allocate pasid from DualStageIOMMU (a.k.a. host IOMMU) */
    int (*pasid_alloc)(DualStageIOMMUObject *dsi_obj,
                       uint32_t min,
                       uint32_t max,
                       uint32_t *pasid);
    /* Reclaim a pasid from DualStageIOMMU (a.k.a. host IOMMU) */
    int (*pasid_free)(DualStageIOMMUObject *dsi_obj,
                      uint32_t pasid);
    /*
     * Bind stage-1 page table to a DualStageIOMMU (a.k.a. host
     * IOMMU which has dual stage DMA translation capability.
     * @bind_data specifies the bind configurations.
     */
    int (*bind_stage1_pgtbl)(DualStageIOMMUObject *dsi_obj,
                            DualIOMMUStage1BindData *bind_data);
    /* Undo a previous bind. @bind_data specifies the unbind info. */
    int (*unbind_stage1_pgtbl)(DualStageIOMMUObject *dsi_obj,
                              DualIOMMUStage1BindData *bind_data);
    /*
     * Propagate stage-1 cache flush to DualStageIOMMU (a.k.a.
     * host IOMMU), cache info specifid in @cache
     */
    int (*flush_stage1_cache)(DualStageIOMMUObject *dsi_obj,
                              DualIOMMUStage1Cache *cache);
};

struct DualStageIOMMUInfo {
    uint32_t pasid_format;
};

/*
 * This is an abstraction of Dual-stage IOMMU.
 */
struct DualStageIOMMUObject {
    DualStageIOMMUOps *ops;
    DualStageIOMMUInfo uinfo;
};

struct DualIOMMUStage1BindData {
    uint32_t pasid;
    union {
        struct iommu_gpasid_bind_data gpasid_bind;
    } bind_data;
};

struct DualIOMMUStage1Cache {
    uint32_t pasid;
    struct iommu_cache_invalidate_info cache_info;
};

int ds_iommu_pasid_alloc(DualStageIOMMUObject *dsi_obj, uint32_t min,
                         uint32_t max, uint32_t *pasid);
int ds_iommu_pasid_free(DualStageIOMMUObject *dsi_obj, uint32_t pasid);
int ds_iommu_bind_stage1_pgtbl(DualStageIOMMUObject *dsi_obj,
                               DualIOMMUStage1BindData *bind_data);
int ds_iommu_unbind_stage1_pgtbl(DualStageIOMMUObject *dsi_obj,
                                 DualIOMMUStage1BindData *bind_data);
int ds_iommu_flush_stage1_cache(DualStageIOMMUObject *dsi_obj,
                                DualIOMMUStage1Cache *cache);

void ds_iommu_object_init(DualStageIOMMUObject *dsi_obj,
                          DualStageIOMMUOps *ops,
                          DualStageIOMMUInfo *uinfo);
void ds_iommu_object_destroy(DualStageIOMMUObject *dsi_obj);

#endif
