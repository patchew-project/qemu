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

int host_iommu_ctx_pasid_alloc(HostIOMMUContext *host_icx, uint32_t min,
                               uint32_t max, uint32_t *pasid)
{
    HostIOMMUContextClass *hicxc;

    if (!host_icx) {
        return -EINVAL;
    }

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(host_icx);

    if (!hicxc) {
        return -EINVAL;
    }

    if (!(host_icx->flags & HOST_IOMMU_PASID_REQUEST) ||
        !hicxc->pasid_alloc) {
        return -EINVAL;
    }

    return hicxc->pasid_alloc(host_icx, min, max, pasid);
}

int host_iommu_ctx_pasid_free(HostIOMMUContext *host_icx, uint32_t pasid)
{
    HostIOMMUContextClass *hicxc;

    if (!host_icx) {
        return -EINVAL;
    }

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(host_icx);
    if (!hicxc) {
        return -EINVAL;
    }

    if (!(host_icx->flags & HOST_IOMMU_PASID_REQUEST) ||
        !hicxc->pasid_free) {
        return -EINVAL;
    }

    return hicxc->pasid_free(host_icx, pasid);
}

int host_iommu_ctx_bind_stage1_pgtbl(HostIOMMUContext *host_icx,
                                     DualIOMMUStage1BindData *data)
{
    HostIOMMUContextClass *hicxc;

    if (!host_icx) {
        return -EINVAL;
    }

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(host_icx);
    if (!hicxc) {
        return -EINVAL;
    }

    if (!(host_icx->flags & HOST_IOMMU_NESTING) ||
        !hicxc->bind_stage1_pgtbl) {
        return -EINVAL;
    }

    return hicxc->bind_stage1_pgtbl(host_icx, data);
}

int host_iommu_ctx_unbind_stage1_pgtbl(HostIOMMUContext *host_icx,
                                       DualIOMMUStage1BindData *data)
{
    HostIOMMUContextClass *hicxc;

    if (!host_icx) {
        return -EINVAL;
    }

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(host_icx);
    if (!hicxc) {
        return -EINVAL;
    }

    if (!(host_icx->flags & HOST_IOMMU_NESTING) ||
        !hicxc->unbind_stage1_pgtbl) {
        return -EINVAL;
    }

    return hicxc->unbind_stage1_pgtbl(host_icx, data);
}

int host_iommu_ctx_flush_stage1_cache(HostIOMMUContext *host_icx,
                                      DualIOMMUStage1Cache *cache)
{
    HostIOMMUContextClass *hicxc;

    hicxc = HOST_IOMMU_CONTEXT_GET_CLASS(host_icx);

    if (!hicxc) {
        return -EINVAL;
    }

    if (!(host_icx->flags & HOST_IOMMU_NESTING) ||
        !hicxc->flush_stage1_cache) {
        return -EINVAL;
    }

    return hicxc->flush_stage1_cache(host_icx, cache);
}

void host_iommu_ctx_init(void *_host_icx, size_t instance_size,
                         const char *mrtypename,
                         uint64_t flags, uint32_t formats)
{
    HostIOMMUContext *host_icx;

    object_initialize(_host_icx, instance_size, mrtypename);
    host_icx = HOST_IOMMU_CONTEXT(_host_icx);
    host_icx->flags = flags;
    host_icx->stage1_formats = formats;
    host_icx->initialized = true;
}

void host_iommu_ctx_destroy(HostIOMMUContext *host_icx)
{
    host_icx->flags = 0x0;
    host_icx->stage1_formats = 0x0;
    host_icx->initialized = false;
}

static void host_icx_init_fn(Object *obj)
{
    HostIOMMUContext *host_icx = HOST_IOMMU_CONTEXT(obj);

    host_icx->flags = 0x0;
    host_icx->stage1_formats = 0x0;
    host_icx->initialized = false;
}

static const TypeInfo host_iommu_context_info = {
    .parent             = TYPE_OBJECT,
    .name               = TYPE_HOST_IOMMU_CONTEXT,
    .class_size         = sizeof(HostIOMMUContextClass),
    .instance_size      = sizeof(HostIOMMUContext),
    .instance_init      = host_icx_init_fn,
    .abstract           = true,
};

static void host_icx_register_types(void)
{
    type_register_static(&host_iommu_context_info);
}

type_init(host_icx_register_types)
