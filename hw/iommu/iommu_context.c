/*
 * QEMU abstract of vIOMMU context
 *
 * Copyright (C) 2020 Red Hat Inc.
 *
 * Authors: Peter Xu <peterx@redhat.com>,
 *          Liu Yi L <yi.l.liu@intel.com>
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

#include "qemu/osdep.h"
#include "hw/iommu/iommu_context.h"

int iommu_context_register_ds_iommu(IOMMUContext *iommu_ctx,
                                    DualStageIOMMUObject *dsi_obj)
{
    if (!iommu_ctx || !dsi_obj) {
        return -ENOENT;
    }

    if (iommu_ctx->ops && iommu_ctx->ops->register_ds_iommu) {
        return iommu_ctx->ops->register_ds_iommu(iommu_ctx, dsi_obj);
    }
    return -ENOENT;
}

void iommu_context_unregister_ds_iommu(IOMMUContext *iommu_ctx,
                                      DualStageIOMMUObject *dsi_obj)
{
    if (!iommu_ctx || !dsi_obj) {
        return;
    }

    if (iommu_ctx->ops && iommu_ctx->ops->unregister_ds_iommu) {
        iommu_ctx->ops->unregister_ds_iommu(iommu_ctx, dsi_obj);
    }
}

void iommu_context_init(IOMMUContext *iommu_ctx, IOMMUContextOps *ops)
{
    iommu_ctx->ops = ops;
}
