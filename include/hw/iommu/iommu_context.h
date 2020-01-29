/*
 * QEMU abstraction of IOMMU Context
 *
 * Copyright (C) 2020 Red Hat Inc.
 *
 * Authors: Peter Xu <peterx@redhat.com>,
 *          Liu, Yi L <yi.l.liu@intel.com>
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
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif
#include "hw/iommu/dual_stage_iommu.h"

typedef struct IOMMUContext IOMMUContext;
typedef struct IOMMUContextOps IOMMUContextOps;

struct IOMMUContextOps {
    /*
     * Register DualStageIOMMUObject to vIOMMU thus vIOMMU
     * is aware of dual stage translation capability, and
     * also be able to setup dual stage translation via
     * interfaces exposed by DualStageIOMMUObject.
     */
    int (*register_ds_iommu)(IOMMUContext *iommu_ctx,
                             DualStageIOMMUObject *dsi_obj);
    void (*unregister_ds_iommu)(IOMMUContext *iommu_ctx,
                                DualStageIOMMUObject *dsi_obj);
};

/*
 * This is an abstraction of IOMMU context.
 */
struct IOMMUContext {
    IOMMUContextOps *ops;
};

int iommu_context_register_ds_iommu(IOMMUContext *iommu_ctx,
                                    DualStageIOMMUObject *dsi_obj);
void iommu_context_unregister_ds_iommu(IOMMUContext *iommu_ctx,
                                       DualStageIOMMUObject *dsi_obj);
void iommu_context_init(IOMMUContext *iommu_ctx, IOMMUContextOps *ops);

#endif
