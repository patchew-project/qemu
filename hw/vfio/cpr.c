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

static int
vfio_region_remap(MemoryRegionSection *section, void *handle, Error **errp)
{
    VFIOContainer *container = handle;
    vfio_container_region_add(container, section, true);
    return 0;
}

bool vfio_is_cpr_capable(VFIOContainer *container, Error **errp)
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

/*
 * Verify that all containers support CPR, and unmap all dma vaddr's.
 */
int vfio_cpr_save(Error **errp)
{
    ERRP_GUARD();
    VFIOAddressSpace *space;
    VFIOContainer *container, *last_container;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            if (!vfio_is_cpr_capable(container, errp)) {
                return -1;
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
    last_container = container;
    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            Error *err;

            if (container == last_container) {
                break;
            }

            /* Set reused so vfio_dma_map restores vaddr */
            container->reused = true;
            if (address_space_flat_for_each_section(space->as,
                                                    vfio_region_remap,
                                                    container, &err)) {
                error_prepend(errp, "%s", error_get_pretty(err));
                error_free(err);
            }
            container->reused = false;
        }
    }
    return -1;
}

/*
 * Register the listener for each container, which causes its callback to be
 * invoked for every flat section.  The callback will see that the container
 * is reused, and call vfo_dma_map with the new vaddr.
 */
int vfio_cpr_load(Error **errp)
{
    VFIOAddressSpace *space;
    VFIOContainer *container;
    VFIOGroup *group;
    VFIODevice *vbasedev;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        QLIST_FOREACH(container, &space->containers, next) {
            if (!vfio_is_cpr_capable(container, errp)) {
                return -1;
            }
            vfio_listener_register(container);
            container->reused = false;
        }
    }
    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            vbasedev->reused = false;
        }
    }
    return 0;
}
