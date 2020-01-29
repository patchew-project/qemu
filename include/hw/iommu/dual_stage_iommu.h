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
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

typedef struct DualStageIOMMUObject DualStageIOMMUObject;
typedef struct DualStageIOMMUOps DualStageIOMMUOps;

struct DualStageIOMMUOps {
    /* Allocate pasid from DualStageIOMMU (a.k.a. host IOMMU) */
    int (*pasid_alloc)(DualStageIOMMUObject *dsi_obj,
                       uint32_t min,
                       uint32_t max,
                       uint32_t *pasid);
    /* Reclaim a pasid from DualStageIOMMU (a.k.a. host IOMMU) */
    int (*pasid_free)(DualStageIOMMUObject *dsi_obj,
                      uint32_t pasid);
};

/*
 * This is an abstraction of Dual-stage IOMMU.
 */
struct DualStageIOMMUObject {
    DualStageIOMMUOps *ops;
};

int ds_iommu_pasid_alloc(DualStageIOMMUObject *dsi_obj, uint32_t min,
                         uint32_t max, uint32_t *pasid);
int ds_iommu_pasid_free(DualStageIOMMUObject *dsi_obj, uint32_t pasid);

void ds_iommu_object_init(DualStageIOMMUObject *dsi_obj,
                          DualStageIOMMUOps *ops);
void ds_iommu_object_destroy(DualStageIOMMUObject *dsi_obj);

#endif
