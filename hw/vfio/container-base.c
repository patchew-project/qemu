/*
 * VFIO BASE CONTAINER
 *
 * Copyright (C) 2023 Intel Corporation.
 * Copyright Red Hat, Inc. 2023
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
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
#include "qemu/error-report.h"
#include "hw/vfio/vfio-container-base.h"

VFIODevice *vfio_container_dev_iter_next(VFIOContainer *container,
                                 VFIODevice *curr)
{
    if (!container->ops->dev_iter_next) {
        return NULL;
    }

    return container->ops->dev_iter_next(container, curr);
}

int vfio_container_dma_map(VFIOContainer *container,
                           hwaddr iova, ram_addr_t size,
                           void *vaddr, bool readonly)
{
    if (!container->ops->dma_map) {
        return -EINVAL;
    }

    return container->ops->dma_map(container, iova, size, vaddr, readonly);
}

int vfio_container_dma_unmap(VFIOContainer *container,
                             hwaddr iova, ram_addr_t size,
                             IOMMUTLBEntry *iotlb)
{
    if (!container->ops->dma_unmap) {
        return -EINVAL;
    }

    return container->ops->dma_unmap(container, iova, size, iotlb);
}

int vfio_container_set_dirty_page_tracking(VFIOContainer *container,
                                            bool start)
{
    if (!container->ops->set_dirty_page_tracking) {
        return -EINVAL;
    }

    return container->ops->set_dirty_page_tracking(container, start);
}

int vfio_container_query_dirty_bitmap(VFIOContainer *container,
                                      VFIOBitmap *vbmap,
                                      hwaddr iova, hwaddr size)
{
    if (!container->ops->query_dirty_bitmap) {
        return -EINVAL;
    }

    return container->ops->query_dirty_bitmap(container, vbmap, iova, size);
}

int vfio_container_add_section_window(VFIOContainer *container,
                                      MemoryRegionSection *section,
                                      Error **errp)
{
    if (!container->ops->add_window) {
        return 0;
    }

    return container->ops->add_window(container, section, errp);
}

void vfio_container_del_section_window(VFIOContainer *container,
                                       MemoryRegionSection *section)
{
    if (!container->ops->del_window) {
        return;
    }

    return container->ops->del_window(container, section);
}

void vfio_container_init(VFIOContainer *container,
                         VFIOAddressSpace *space,
                         struct VFIOIOMMUBackendOpsClass *ops)
{
    container->ops = ops;
    container->space = space;
    container->error = NULL;
    container->dirty_pages_supported = false;
    container->dma_max_mappings = 0;
    QLIST_INIT(&container->giommu_list);
    QLIST_INIT(&container->hostwin_list);
    QLIST_INIT(&container->vrdl_list);
}

void vfio_container_destroy(VFIOContainer *container)
{
    VFIORamDiscardListener *vrdl, *vrdl_tmp;
    VFIOGuestIOMMU *giommu, *tmp;
    VFIOHostDMAWindow *hostwin, *next;

    QLIST_SAFE_REMOVE(container, next);

    QLIST_FOREACH_SAFE(vrdl, &container->vrdl_list, next, vrdl_tmp) {
        RamDiscardManager *rdm;

        rdm = memory_region_get_ram_discard_manager(vrdl->mr);
        ram_discard_manager_unregister_listener(rdm, &vrdl->listener);
        QLIST_REMOVE(vrdl, next);
        g_free(vrdl);
    }

    QLIST_FOREACH_SAFE(giommu, &container->giommu_list, giommu_next, tmp) {
        memory_region_unregister_iommu_notifier(
                MEMORY_REGION(giommu->iommu_mr), &giommu->n);
        QLIST_REMOVE(giommu, giommu_next);
        g_free(giommu);
    }

    QLIST_FOREACH_SAFE(hostwin, &container->hostwin_list, hostwin_next,
                       next) {
        QLIST_REMOVE(hostwin, hostwin_next);
        g_free(hostwin);
    }
}
