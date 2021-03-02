/*
 * QEMU abstract of Host IOMMU
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/iommu/host_iommu_context.h"

int host_iommu_ctx_bind_stage1_pgtbl(HostIOMMUContext *iommu_ctx,
                                     struct iommu_gpasid_bind_data *bind)
{
    HostIOMMUContextClass *hicxc;

    if (!iommu_ctx) {
        return -EINVAL;
    }

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(iommu_ctx);
    if (!hicxc) {
        return -EINVAL;
    }

    if (!(iommu_ctx->info->features & IOMMU_NESTING_FEAT_BIND_PGTBL) ||
        !hicxc->bind_stage1_pgtbl) {
        return -EINVAL;
    }

    return hicxc->bind_stage1_pgtbl(iommu_ctx, bind);
}

int host_iommu_ctx_unbind_stage1_pgtbl(HostIOMMUContext *iommu_ctx,
                                 struct iommu_gpasid_bind_data *unbind)
{
    HostIOMMUContextClass *hicxc;

    if (!iommu_ctx) {
        return -EINVAL;
    }

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(iommu_ctx);
    if (!hicxc) {
        return -EINVAL;
    }

    if (!(iommu_ctx->info->features & IOMMU_NESTING_FEAT_BIND_PGTBL) ||
        !hicxc->unbind_stage1_pgtbl) {
        return -EINVAL;
    }

    return hicxc->unbind_stage1_pgtbl(iommu_ctx, unbind);
}

int host_iommu_ctx_flush_stage1_cache(HostIOMMUContext *iommu_ctx,
                                 struct iommu_cache_invalidate_info *cache)
{
    HostIOMMUContextClass *hicxc;

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(iommu_ctx);

    if (!hicxc) {
        return -EINVAL;
    }

    if (!(iommu_ctx->info->features & IOMMU_NESTING_FEAT_CACHE_INVLD) ||
        !hicxc->flush_stage1_cache) {
        return -EINVAL;
    }

    return hicxc->flush_stage1_cache(iommu_ctx, cache);
}

void host_iommu_ctx_init(void *_iommu_ctx, size_t instance_size,
                         const char *mrtypename,
                         struct iommu_nesting_info *info)
{
    HostIOMMUContext *iommu_ctx;

    object_initialize(_iommu_ctx, instance_size, mrtypename);
    iommu_ctx = HOST_IOMMU_CONTEXT(_iommu_ctx);
    iommu_ctx->info = g_malloc0(info->argsz);
    memcpy(iommu_ctx->info, info, info->argsz);
    iommu_ctx->initialized = true;
}

static void host_iommu_ctx_finalize_fn(Object *obj)
{
    HostIOMMUContext *iommu_ctx = HOST_IOMMU_CONTEXT(obj);

    g_free(iommu_ctx->info);
}

static const TypeInfo host_iommu_context_info = {
    .parent             = TYPE_OBJECT,
    .name               = TYPE_HOST_IOMMU_CONTEXT,
    .class_size         = sizeof(HostIOMMUContextClass),
    .instance_size      = sizeof(HostIOMMUContext),
    .instance_finalize  = host_iommu_ctx_finalize_fn,
    .abstract           = true,
};

static void host_iommu_ctx_register_types(void)
{
    type_register_static(&host_iommu_context_info);
}

type_init(host_iommu_ctx_register_types)
