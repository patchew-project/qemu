/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "hw/vfio/vfio-common.h"
#include "sysemu/kvm.h"
#include "qapi/error.h"
#include "trace.h"

static int
vfio_dma_unmap_vaddr_all(VFIOContainer *container, Error **errp)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = VFIO_DMA_UNMAP_FLAG_VADDR | VFIO_DMA_UNMAP_FLAG_ALL,
        .iova = 0,
        .size = 0,
    };
    if (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        error_setg_errno(errp, errno, "vfio_dma_unmap_vaddr_all");
        return -errno;
    }
    return 0;
}

static int vfio_dma_map_vaddr(VFIOContainer *container, hwaddr iova,
                              ram_addr_t size, void *vaddr,
                              Error **errp)
{
    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .flags = VFIO_DMA_MAP_FLAG_VADDR,
        .vaddr = (__u64)(uintptr_t)vaddr,
        .iova = iova,
        .size = size,
    };
    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map)) {
        error_setg_errno(errp, errno,
                         "vfio_dma_map_vaddr(iova %lu, size %ld, va %p)",
                         iova, size, vaddr);
        return -errno;
    }
    return 0;
}

static int
vfio_region_remap(MemoryRegionSection *section, void *handle, Error **errp)
{
    MemoryRegion *mr = section->mr;
    VFIOContainer *container = handle;
    const char *name = memory_region_name(mr);
    ram_addr_t size = int128_get64(section->size);
    hwaddr offset, iova, roundup;
    void *vaddr;

    if (vfio_listener_skipped_section(section) || memory_region_is_iommu(mr)) {
        return 0;
    }

    offset = section->offset_within_address_space;
    iova = TARGET_PAGE_ALIGN(offset);
    roundup = iova - offset;
    size = (size - roundup) & TARGET_PAGE_MASK;
    vaddr = memory_region_get_ram_ptr(mr) +
            section->offset_within_region + roundup;

    trace_vfio_region_remap(name, container->fd, iova, iova + size - 1, vaddr);
    return vfio_dma_map_vaddr(container, iova, size, vaddr, errp);
}

bool vfio_cpr_capable(VFIOContainer *container, Error **errp)
{
    if (!ioctl(container->fd, VFIO_CHECK_EXTENSION, VFIO_UPDATE_VADDR) ||
        !ioctl(container->fd, VFIO_CHECK_EXTENSION, VFIO_UNMAP_ALL)) {
        error_setg(errp, "VFIO container does not support VFIO_UPDATE_VADDR "
                         "or VFIO_UNMAP_ALL");
        return false;
    } else {
        return true;
    }
}

int vfio_cprsave(Error **errp)
{
    VFIOAddressSpace *space, *last_space;
    VFIOContainer *container, *last_container;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            if (!vfio_cpr_capable(container, errp)) {
                return 1;
            }
        }
    }

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            if (vfio_dma_unmap_vaddr_all(container, errp)) {
                goto unwind;
            }
        }
    }
    return 0;

unwind:
    last_space = space;
    last_container = container;
    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            Error *err;

            if (space == last_space && container == last_container) {
                break;
            }
            if (as_flat_walk(space->as, vfio_region_remap, container, &err)) {
                error_prepend(errp, "%s", error_get_pretty(err));
                error_free(err);
            }
        }
    }
    return 1;
}

int vfio_cprload(Error **errp)
{
    VFIOAddressSpace *space;
    VFIOContainer *container;
    VFIOGroup *group;
    VFIODevice *vbasedev;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            if (!vfio_cpr_capable(container, errp)) {
                return 1;
            }
            container->reused = false;
            if (as_flat_walk(space->as, vfio_region_remap, container, errp)) {
                return 1;
            }
        }
    }
    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            vbasedev->reused = false;
        }
    }
    return 0;
}
