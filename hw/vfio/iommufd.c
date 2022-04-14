/*
 * iommufd container backend
 *
 * Copyright (C) 2022 Intel Corporation.
 * Copyright Red Hat, Inc. 2022
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
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/iommufd/iommufd.h"
#include "hw/qdev-core.h"
#include "sysemu/reset.h"
#include "qemu/cutils.h"

static bool iommufd_check_extension(VFIOContainer *bcontainer,
                                    VFIOContainerFeature feat)
{
    switch (feat) {
    default:
        return false;
    };
}

static int iommufd_map(VFIOContainer *bcontainer, hwaddr iova,
                       ram_addr_t size, void *vaddr, bool readonly)
{
    VFIOIOMMUFDContainer *container = container_of(bcontainer,
                                                   VFIOIOMMUFDContainer, obj);

    return iommufd_map_dma(container->iommufd, container->ioas_id,
                           iova, size, vaddr, readonly);
}

static int iommufd_unmap(VFIOContainer *bcontainer,
                         hwaddr iova, ram_addr_t size,
                         IOMMUTLBEntry *iotlb)
{
    VFIOIOMMUFDContainer *container = container_of(bcontainer,
                                                   VFIOIOMMUFDContainer, obj);

    /* TODO: Handle dma_unmap_bitmap with iotlb args (migration) */
    return iommufd_unmap_dma(container->iommufd,
                             container->ioas_id, iova, size);
}

static int vfio_get_devicefd(const char *sysfs_path, Error **errp)
{
    long int vfio_id = -1, ret = -ENOTTY;
    char *path, *tmp = NULL;
    DIR *dir;
    struct dirent *dent;
    struct stat st;
    gchar *contents;
    gsize length;
    int major, minor;
    dev_t vfio_devt;

    path = g_strdup_printf("%s/vfio-device", sysfs_path);
    if (stat(path, &st) < 0) {
        error_setg_errno(errp, errno, "no such host device");
        goto out;
    }

    dir = opendir(path);
    if (!dir) {
        error_setg_errno(errp, errno, "couldn't open dirrectory %s", path);
        goto out;
    }

    while ((dent = readdir(dir))) {
        const char *end_name;

        if (!strncmp(dent->d_name, "vfio", 4)) {
            ret = qemu_strtol(dent->d_name + 4, &end_name, 10, &vfio_id);
            if (ret) {
                error_setg(errp, "suspicious vfio* file in %s", path);
                goto out;
            }
            break;
        }
    }

    /* check if the major:minor matches */
    tmp = g_strdup_printf("%s/%s/dev", path, dent->d_name);
    if (!g_file_get_contents(tmp, &contents, &length, NULL)) {
        error_setg(errp, "failed to load \"%s\"", tmp);
        goto out;
    }

    if (sscanf(contents, "%d:%d", &major, &minor) != 2) {
        error_setg(errp, "failed to load \"%s\"", tmp);
        goto out;
    }
    g_free(contents);
    g_free(tmp);

    tmp = g_strdup_printf("/dev/vfio/devices/vfio%ld", vfio_id);
    if (stat(tmp, &st) < 0) {
        error_setg_errno(errp, errno, "no such vfio device");
        goto out;
    }
    vfio_devt = makedev(major, minor);
    if (st.st_rdev != vfio_devt) {
        error_setg(errp, "minor do not match: %lu, %lu", vfio_devt, st.st_rdev);
        goto out;
    }

    ret = qemu_open_old(tmp, O_RDWR);
    if (ret < 0) {
        error_setg(errp, "Failed to open %s", tmp);
    }
    trace_vfio_iommufd_get_devicefd(tmp, ret);
out:
    g_free(tmp);
    g_free(path);

    if (*errp) {
        error_prepend(errp, VFIO_MSG_PREFIX, path);
    }
    return ret;
}

static VFIOIOASHwpt *vfio_container_get_hwpt(VFIOIOMMUFDContainer *container,
                                             uint32_t hwpt_id)
{
    VFIOIOASHwpt *hwpt;

    QLIST_FOREACH(hwpt, &container->hwpt_list, next) {
        if (hwpt->hwpt_id == hwpt_id) {
            return hwpt;
        }
    }

    hwpt = g_malloc0(sizeof(*hwpt));

    hwpt->hwpt_id = hwpt_id;
    QLIST_INIT(&hwpt->device_list);
    QLIST_INSERT_HEAD(&container->hwpt_list, hwpt, next);

    return hwpt;
}

static void vfio_container_put_hwpt(VFIOIOASHwpt *hwpt)
{
    if (!QLIST_EMPTY(&hwpt->device_list)) {
        g_assert_not_reached();
    }
    QLIST_REMOVE(hwpt, next);
    g_free(hwpt);
}

static VFIOIOASHwpt *vfio_find_hwpt_for_dev(VFIOIOMMUFDContainer *container,
                                            VFIODevice *vbasedev)
{
    VFIOIOASHwpt *hwpt;
    VFIODevice *vbasedev_iter;

    QLIST_FOREACH(hwpt, &container->hwpt_list, next) {
        QLIST_FOREACH(vbasedev_iter, &hwpt->device_list, hwpt_next) {
            if (vbasedev_iter == vbasedev) {
                return hwpt;
            }
        }
    }
    return NULL;
}

static void
__vfio_device_detach_container(VFIODevice *vbasedev,
                               VFIOIOMMUFDContainer *container, Error **errp)
{
    struct vfio_device_detach_ioas detach_data = {
        .argsz = sizeof(detach_data),
        .flags = 0,
        .iommufd = container->iommufd,
        .ioas_id = container->ioas_id,
    };

    if (ioctl(vbasedev->fd, VFIO_DEVICE_DETACH_IOAS, &detach_data)) {
        error_setg_errno(errp, errno, "detach %s from ioas id=%d failed",
                         vbasedev->name, container->ioas_id);
    }
    trace_vfio_iommufd_detach_device(container->iommufd, vbasedev->name,
                                     container->ioas_id);

    /* iommufd unbind is done per device fd close */
}

static void vfio_device_detach_container(VFIODevice *vbasedev,
                                         VFIOIOMMUFDContainer *container,
                                         Error **errp)
{
    VFIOIOASHwpt *hwpt;

    hwpt = vfio_find_hwpt_for_dev(container, vbasedev);
    if (hwpt) {
        QLIST_REMOVE(vbasedev, hwpt_next);
        if (QLIST_EMPTY(&hwpt->device_list)) {
            vfio_container_put_hwpt(hwpt);
        }
    }

    __vfio_device_detach_container(vbasedev, container, errp);
}

static int vfio_device_attach_container(VFIODevice *vbasedev,
                                        VFIOIOMMUFDContainer *container,
                                        Error **errp)
{
    struct vfio_device_bind_iommufd bind = {
        .argsz = sizeof(bind),
        .flags = 0,
        .iommufd = container->iommufd,
        .dev_cookie = (uint64_t)vbasedev,
    };
    struct vfio_device_attach_ioas attach_data = {
        .argsz = sizeof(attach_data),
        .flags = 0,
        .iommufd = container->iommufd,
        .ioas_id = container->ioas_id,
    };
    VFIOIOASHwpt *hwpt;
    int ret;

    /* Bind device to iommufd */
    ret = ioctl(vbasedev->fd, VFIO_DEVICE_BIND_IOMMUFD, &bind);
    if (ret) {
        error_setg_errno(errp, errno, "error bind device fd=%d to iommufd=%d",
                         vbasedev->fd, bind.iommufd);
        return ret;
    }

    vbasedev->devid = bind.out_devid;
    trace_vfio_iommufd_bind_device(bind.iommufd, vbasedev->name,
                                   vbasedev->fd, vbasedev->devid);

    /* Attach device to an ioas within iommufd */
    ret = ioctl(vbasedev->fd, VFIO_DEVICE_ATTACH_IOAS, &attach_data);
    if (ret) {
        error_setg_errno(errp, errno,
                         "[iommufd=%d] error attach %s (%d) to ioasid=%d",
                         container->iommufd, vbasedev->name, vbasedev->fd,
                         attach_data.ioas_id);
        return ret;

    }
    trace_vfio_iommufd_attach_device(bind.iommufd, vbasedev->name,
                                     vbasedev->fd, container->ioas_id,
                                     attach_data.out_hwpt_id);

    hwpt = vfio_container_get_hwpt(container, attach_data.out_hwpt_id);

    QLIST_INSERT_HEAD(&hwpt->device_list, vbasedev, hwpt_next);
    return 0;
}

static int vfio_device_reset(VFIODevice *vbasedev)
{
    if (vbasedev->dev->realized) {
        vbasedev->ops->vfio_compute_needs_reset(vbasedev);
        if (vbasedev->needs_reset) {
            return vbasedev->ops->vfio_hot_reset_multi(vbasedev);
        }
    }
    return 0;
}

static int vfio_iommufd_container_reset(VFIOContainer *bcontainer)
{
    VFIOIOMMUFDContainer *container;
    int ret, final_ret = 0;
    VFIODevice *vbasedev;
    VFIOIOASHwpt *hwpt;

    container = container_of(bcontainer, VFIOIOMMUFDContainer, obj);

    QLIST_FOREACH(hwpt, &container->hwpt_list, next) {
        QLIST_FOREACH(vbasedev, &hwpt->device_list, hwpt_next) {
            ret = vfio_device_reset(vbasedev);
            if (ret) {
                error_report("failed to reset %s (%d)", vbasedev->name, ret);
                final_ret = ret;
            } else {
                trace_vfio_iommufd_container_reset(vbasedev->name);
            }
        }
    }
    return final_ret;
}

static void vfio_iommufd_container_destroy(VFIOIOMMUFDContainer *container)
{
    vfio_container_destroy(&container->obj);
    g_free(container);
}

static int vfio_ram_block_discard_disable(bool state)
{
    /*
     * We support coordinated discarding of RAM via the RamDiscardManager.
     */
    return ram_block_uncoordinated_discard_disable(state);
}

static void iommufd_detach_device(VFIODevice *vbasedev);

static int iommufd_attach_device(VFIODevice *vbasedev, AddressSpace *as,
                                 Error **errp)
{
    VFIOContainer *bcontainer;
    VFIOIOMMUFDContainer *container;
    VFIOAddressSpace *space;
    struct vfio_device_info dev_info = { .argsz = sizeof(dev_info) };
    int ret, devfd, iommufd;
    uint32_t ioas_id;
    Error *err = NULL;

    devfd = vfio_get_devicefd(vbasedev->sysfsdev, errp);
    if (devfd < 0) {
        return devfd;
    }
    vbasedev->fd = devfd;

    space = vfio_get_address_space(as);

    /* try to attach to an existing container in this space */
    QLIST_FOREACH(bcontainer, &space->containers, next) {
        if (!object_dynamic_cast(OBJECT(bcontainer),
                                 TYPE_VFIO_IOMMUFD_CONTAINER)) {
            continue;
        }
        container = container_of(bcontainer, VFIOIOMMUFDContainer, obj);
        if (vfio_device_attach_container(vbasedev, container, &err)) {
            const char *msg = error_get_pretty(err);

            trace_vfio_iommufd_fail_attach_existing_container(msg);
            error_free(err);
            err = NULL;
        } else {
            ret = vfio_ram_block_discard_disable(true);
            if (ret) {
                vfio_device_detach_container(vbasedev, container, &err);
                error_propagate(errp, err);
                vfio_put_address_space(space);
                close(vbasedev->fd);
                error_prepend(errp,
                              "Cannot set discarding of RAM broken (%d)", ret);
                return ret;
            }
            goto out;
        }
    }

    /* Need to allocate a new dedicated container */
    ret = iommufd_get_ioas(&iommufd, &ioas_id);
    if (ret < 0) {
        vfio_put_address_space(space);
        close(vbasedev->fd);
        error_report("Failed to alloc ioas (%s)", strerror(errno));
        return ret;
    }

    trace_vfio_iommufd_alloc_ioas(iommufd, ioas_id);

    container = g_malloc0(sizeof(*container));
    container->iommufd = iommufd;
    container->ioas_id = ioas_id;
    QLIST_INIT(&container->hwpt_list);

    bcontainer = &container->obj;
    vfio_container_init(bcontainer, sizeof(*bcontainer),
                        TYPE_VFIO_IOMMUFD_CONTAINER, space);

    ret = vfio_device_attach_container(vbasedev, container, &err);
    if (ret) {
        /* todo check if any other thing to do */
        error_propagate(errp, err);
        vfio_iommufd_container_destroy(container);
        iommufd_put_ioas(iommufd, ioas_id);
        vfio_put_address_space(space);
        close(vbasedev->fd);
        return ret;
    }

    ret = vfio_ram_block_discard_disable(true);
    if (ret) {
        vfio_device_detach_container(vbasedev, container, &err);
        error_propagate(errp, err);
        error_prepend(errp, "Cannot set discarding of RAM broken (%d)", -ret);
        vfio_iommufd_container_destroy(container);
        iommufd_put_ioas(iommufd, ioas_id);
        vfio_put_address_space(space);
        close(vbasedev->fd);
        return ret;
    }

    /*
     * TODO: for now iommufd BE is on par with vfio iommu type1, so it's
     * fine to add the whole range as window. For SPAPR, below code
     * should be updated.
     */
    vfio_host_win_add(bcontainer, 0, (hwaddr)-1, 4096);

    /*
     * TODO: kvmgroup, unable to do it before the protocol done
     * between iommufd and kvm.
     */

    QLIST_INSERT_HEAD(&space->containers, bcontainer, next);

    bcontainer->listener = vfio_memory_listener;

    memory_listener_register(&bcontainer->listener, bcontainer->space->as);

    bcontainer->initialized = true;

out:
    vbasedev->container = bcontainer;

    /*
     * TODO: examine RAM_BLOCK_DISCARD stuff, should we do group level
     * for discarding incompatibility check as well?
     */
    if (vbasedev->ram_block_discard_allowed) {
        vfio_ram_block_discard_disable(false);
    }

    ret = ioctl(devfd, VFIO_DEVICE_GET_INFO, &dev_info);
    if (ret) {
        error_setg_errno(errp, errno, "error getting device info");
        /*
         * Needs to use iommufd_detach_device() as this may be failed after
         * attaching a new deivce to an existing container.
         */
        iommufd_detach_device(vbasedev);
        close(vbasedev->fd);
        return ret;
    }

    vbasedev->group = 0;
    vbasedev->num_irqs = dev_info.num_irqs;
    vbasedev->num_regions = dev_info.num_regions;
    vbasedev->flags = dev_info.flags;
    vbasedev->reset_works = !!(dev_info.flags & VFIO_DEVICE_FLAGS_RESET);

    trace_vfio_iommufd_device_info(vbasedev->name, devfd, vbasedev->num_irqs,
                                   vbasedev->num_regions, vbasedev->flags);
    return 0;
}

static void iommufd_detach_device(VFIODevice *vbasedev)
{
    VFIOContainer *bcontainer = vbasedev->container;
    VFIOIOMMUFDContainer *container;
    VFIODevice *vbasedev_iter;
    VFIOIOASHwpt *hwpt;
    Error *err;

    if (!bcontainer) {
        goto out;
    }

    if (!vbasedev->ram_block_discard_allowed) {
        vfio_ram_block_discard_disable(false);
    }

    container = container_of(bcontainer, VFIOIOMMUFDContainer, obj);
    QLIST_FOREACH(hwpt, &container->hwpt_list, next) {
        QLIST_FOREACH(vbasedev_iter, &hwpt->device_list, hwpt_next) {
            if (vbasedev_iter == vbasedev) {
                goto found;
            }
        }
    }
    g_assert_not_reached();
found:
    QLIST_REMOVE(vbasedev, hwpt_next);
    if (QLIST_EMPTY(&hwpt->device_list)) {
        vfio_container_put_hwpt(hwpt);
    }

    __vfio_device_detach_container(vbasedev, container, &err);
    if (err) {
        error_report_err(err);
    }
    if (QLIST_EMPTY(&container->hwpt_list)) {
        VFIOAddressSpace *space = bcontainer->space;

        iommufd_put_ioas(container->iommufd, container->ioas_id);
        vfio_iommufd_container_destroy(container);
        vfio_put_address_space(space);
    }
    vbasedev->container = NULL;
out:
    close(vbasedev->fd);
    g_free(vbasedev->name);
}

static void vfio_iommufd_class_init(ObjectClass *klass,
                                    void *data)
{
    VFIOContainerClass *vccs = VFIO_CONTAINER_OBJ_CLASS(klass);

    vccs->check_extension = iommufd_check_extension;
    vccs->dma_map = iommufd_map;
    vccs->dma_unmap = iommufd_unmap;
    vccs->attach_device = iommufd_attach_device;
    vccs->detach_device = iommufd_detach_device;
    vccs->reset = vfio_iommufd_container_reset;
}

static const TypeInfo vfio_iommufd_info = {
    .parent = TYPE_VFIO_CONTAINER_OBJ,
    .name = TYPE_VFIO_IOMMUFD_CONTAINER,
    .class_init = vfio_iommufd_class_init,
};

static void vfio_iommufd_register_types(void)
{
    type_register_static(&vfio_iommufd_info);
}

type_init(vfio_iommufd_register_types)
