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
#include "qemu/thread.h"
#include "qom/object.h"
#include <linux/iommu.h>
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

#define TYPE_HOST_IOMMU_CONTEXT "qemu:host-iommu-context"
#define HOST_IOMMU_CONTEXT(obj) \
        OBJECT_CHECK(HostIOMMUContext, (obj), TYPE_HOST_IOMMU_CONTEXT)
#define HOST_IOMMU_CONTEXT_CLASS(klass) \
        OBJECT_CLASS_CHECK(HostIOMMUContextClass, (klass), \
                         TYPE_HOST_IOMMU_CONTEXT)
#define HOST_IOMMU_CONTEXT_GET_CLASS(obj) \
        OBJECT_GET_CLASS(HostIOMMUContextClass, (obj), \
                         TYPE_HOST_IOMMU_CONTEXT)

typedef struct HostIOMMUContext HostIOMMUContext;

typedef struct HostIOMMUContextClass {
    /* private */
    ObjectClass parent_class;
    /*
     * Bind stage-1 page table to a hostIOMMU w/ dual stage
     * DMA translation capability.
     * @bind specifies the bind configurations.
     */
    int (*bind_stage1_pgtbl)(HostIOMMUContext *iommu_ctx,
                             struct iommu_gpasid_bind_data *bind);
    /* Undo a previous bind. @unbind specifies the unbind info. */
    int (*unbind_stage1_pgtbl)(HostIOMMUContext *iommu_ctx,
                               struct iommu_gpasid_bind_data *unbind);
    /*
     * Propagate stage-1 cache flush to host IOMMU, cache
     * info specifid in @cache
     */
    int (*flush_stage1_cache)(HostIOMMUContext *iommu_ctx,
                              struct iommu_cache_invalidate_info *cache);
} HostIOMMUContextClass;

/*
 * This is an abstraction of host IOMMU with dual-stage capability
 */
struct HostIOMMUContext {
    Object parent_obj;
    struct iommu_nesting_info *info;
    bool initialized;
};

int host_iommu_ctx_bind_stage1_pgtbl(HostIOMMUContext *iommu_ctx,
                                     struct iommu_gpasid_bind_data *bind);
int host_iommu_ctx_unbind_stage1_pgtbl(HostIOMMUContext *iommu_ctx,
                                 struct iommu_gpasid_bind_data *unbind);
int host_iommu_ctx_flush_stage1_cache(HostIOMMUContext *iommu_ctx,
                               struct iommu_cache_invalidate_info *cache);
void host_iommu_ctx_init(void *_iommu_ctx, size_t instance_size,
                         const char *mrtypename,
                         struct iommu_nesting_info *info);
void host_iommu_ctx_destroy(HostIOMMUContext *iommu_ctx);

#endif
