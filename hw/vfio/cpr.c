/*
 * Copyright (c) 2021, 2022 Oracle and/or its affiliates.
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
#include "migration/blocker.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "migration/vmstate.h"
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
    container->vaddr_unmapped = true;
    return 0;
}

static bool vfio_is_cpr_capable(VFIOContainer *container, Error **errp)
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

static bool vfio_vmstate_needed(void *opaque)
{
    return migrate_mode() == MIG_MODE_CPR_EXEC;
}

static int vfio_container_pre_save(void *opaque)
{
    VFIOContainer *container = (VFIOContainer *)opaque;
    Error *err = NULL;

    if (!vfio_is_cpr_capable(container, &err) ||
        vfio_dma_unmap_vaddr_all(container, &err)) {
        error_report_err(err);
        return -1;
    }
    return 0;
}

static int vfio_container_post_load(void *opaque, int version_id)
{
    VFIOContainer *container = (VFIOContainer *)opaque;
    VFIOGroup *group;
    Error *err = NULL;
    VFIODevice *vbasedev;

    if (!vfio_is_cpr_capable(container, &err)) {
        error_report_err(err);
        return -1;
    }

    vfio_listener_register(container);
    container->reused = false;

    QLIST_FOREACH(group, &container->group_list, container_next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            vbasedev->reused = false;
        }
    }
    return 0;
}

static const VMStateDescription vfio_container_vmstate = {
    .name = "vfio-container",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = vfio_container_pre_save,
    .post_load = vfio_container_post_load,
    .needed = vfio_vmstate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

int vfio_cpr_register_container(VFIOContainer *container, Error **errp)
{
    container->cpr_blocker = NULL;
    if (!vfio_is_cpr_capable(container, &container->cpr_blocker)) {
        return migrate_add_blockers(&container->cpr_blocker, errp,
                                    MIG_MODE_CPR_EXEC, -1);
    }

    vmstate_register(NULL, -1, &vfio_container_vmstate, container);

    return 0;
}

void vfio_cpr_unregister_container(VFIOContainer *container)
{
    migrate_del_blocker(&container->cpr_blocker);

    vmstate_unregister(NULL, &vfio_container_vmstate, container);
}
