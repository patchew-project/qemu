/*
 * iommufd container backend
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
#include "sysemu/iommufd.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "monitor/monitor.h"
#include "trace.h"
#include <sys/ioctl.h>
#include <linux/iommufd.h>

static void iommufd_backend_init(Object *obj)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(obj);

    be->fd = -1;
    be->users = 0;
    be->owned = true;
    qemu_mutex_init(&be->lock);
}

static void iommufd_backend_finalize(Object *obj)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(obj);

    if (be->owned) {
        close(be->fd);
        be->fd = -1;
    }
}

static void iommufd_backend_set_fd(Object *obj, const char *str, Error **errp)
{
    IOMMUFDBackend *be = IOMMUFD_BACKEND(obj);
    int fd = -1;

    fd = monitor_fd_param(monitor_cur(), str, errp);
    if (fd == -1) {
        error_prepend(errp, "Could not parse remote object fd %s:", str);
        return;
    }
    qemu_mutex_lock(&be->lock);
    be->fd = fd;
    be->owned = false;
    qemu_mutex_unlock(&be->lock);
    trace_iommu_backend_set_fd(be->fd);
}

static void iommufd_backend_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "fd", NULL, iommufd_backend_set_fd);
}

int iommufd_backend_connect(IOMMUFDBackend *be, Error **errp)
{
    int fd, ret = 0;

    qemu_mutex_lock(&be->lock);
    if (be->users == UINT32_MAX) {
        error_setg(errp, "too many connections");
        ret = -E2BIG;
        goto out;
    }
    if (be->owned && !be->users) {
        fd = qemu_open_old("/dev/iommu", O_RDWR);
        if (fd < 0) {
            error_setg_errno(errp, errno, "/dev/iommu opening failed");
            ret = fd;
            goto out;
        }
        be->fd = fd;
    }
    be->users++;
out:
    trace_iommufd_backend_connect(be->fd, be->owned,
                                  be->users, ret);
    qemu_mutex_unlock(&be->lock);
    return ret;
}

void iommufd_backend_disconnect(IOMMUFDBackend *be)
{
    qemu_mutex_lock(&be->lock);
    if (!be->users) {
        goto out;
    }
    be->users--;
    if (!be->users && be->owned) {
        close(be->fd);
        be->fd = -1;
    }
out:
    trace_iommufd_backend_disconnect(be->fd, be->users);
    qemu_mutex_unlock(&be->lock);
}

static int iommufd_backend_alloc_ioas(int fd, uint32_t *ioas)
{
    int ret;
    struct iommu_ioas_alloc alloc_data  = {
        .size = sizeof(alloc_data),
        .flags = 0,
    };

    ret = ioctl(fd, IOMMU_IOAS_ALLOC, &alloc_data);
    if (ret) {
        error_report("Failed to allocate ioas %m");
    }

    *ioas = alloc_data.out_ioas_id;
    trace_iommufd_backend_alloc_ioas(fd, *ioas, ret);

    return ret;
}

void iommufd_backend_free_id(int fd, uint32_t id)
{
    int ret;
    struct iommu_destroy des = {
        .size = sizeof(des),
        .id = id,
    };

    ret = ioctl(fd, IOMMU_DESTROY, &des);
    trace_iommufd_backend_free_id(fd, id, ret);
    if (ret) {
        error_report("Failed to free id: %u %m", id);
    }
}

int iommufd_backend_get_ioas(IOMMUFDBackend *be, uint32_t *ioas_id)
{
    int ret;

    ret = iommufd_backend_alloc_ioas(be->fd, ioas_id);
    trace_iommufd_backend_get_ioas(be->fd, *ioas_id, ret);
    return ret;
}

void iommufd_backend_put_ioas(IOMMUFDBackend *be, uint32_t ioas)
{
    trace_iommufd_backend_put_ioas(be->fd, ioas);
    iommufd_backend_free_id(be->fd, ioas);
}

int iommufd_backend_unmap_dma(IOMMUFDBackend *be, uint32_t ioas,
                              hwaddr iova, ram_addr_t size)
{
    int ret;
    struct iommu_ioas_unmap unmap = {
        .size = sizeof(unmap),
        .ioas_id = ioas,
        .iova = iova,
        .length = size,
    };

    ret = ioctl(be->fd, IOMMU_IOAS_UNMAP, &unmap);
    trace_iommufd_backend_unmap_dma(be->fd, ioas, iova, size, ret);
    if (ret && errno == ENOENT) {
        ret = 0;
    }
    if (ret) {
        error_report("IOMMU_IOAS_UNMAP failed: %s", strerror(errno));
    }
    return !ret ? 0 : -errno;
}

int iommufd_backend_map_dma(IOMMUFDBackend *be, uint32_t ioas, hwaddr iova,
                            ram_addr_t size, void *vaddr, bool readonly)
{
    int ret;
    struct iommu_ioas_map map = {
        .size = sizeof(map),
        .flags = IOMMU_IOAS_MAP_READABLE |
                 IOMMU_IOAS_MAP_FIXED_IOVA,
        .ioas_id = ioas,
        .__reserved = 0,
        .user_va = (int64_t)vaddr,
        .iova = iova,
        .length = size,
    };

    if (!readonly) {
        map.flags |= IOMMU_IOAS_MAP_WRITEABLE;
    }

    ret = ioctl(be->fd, IOMMU_IOAS_MAP, &map);
    trace_iommufd_backend_map_dma(be->fd, ioas, iova, size,
                                  vaddr, readonly, ret);
    if (ret) {
        error_report("IOMMU_IOAS_MAP failed: %s", strerror(errno));
    }
    return !ret ? 0 : -errno;
}

int iommufd_backend_copy_dma(IOMMUFDBackend *be, uint32_t src_ioas,
                             uint32_t dst_ioas, hwaddr iova,
                             ram_addr_t size, bool readonly)
{
    int ret;
    struct iommu_ioas_copy copy = {
        .size = sizeof(copy),
        .flags = IOMMU_IOAS_MAP_READABLE |
                 IOMMU_IOAS_MAP_FIXED_IOVA,
        .dst_ioas_id = dst_ioas,
        .src_ioas_id = src_ioas,
        .length = size,
        .dst_iova = iova,
        .src_iova = iova,
    };

    if (!readonly) {
        copy.flags |= IOMMU_IOAS_MAP_WRITEABLE;
    }

    ret = ioctl(be->fd, IOMMU_IOAS_COPY, &copy);
    trace_iommufd_backend_copy_dma(be->fd, src_ioas, dst_ioas,
                                   iova, size, readonly, ret);
    if (ret) {
        error_report("IOMMU_IOAS_COPY failed: %s", strerror(errno));
    }
    return !ret ? 0 : -errno;
}

int iommufd_backend_alloc_hwpt(int iommufd, uint32_t dev_id,
                               uint32_t pt_id, uint32_t *out_hwpt)
{
    int ret;
    struct iommu_hwpt_alloc alloc_hwpt = {
        .size = sizeof(struct iommu_hwpt_alloc),
        .flags = 0,
        .dev_id = dev_id,
        .pt_id = pt_id,
        .__reserved = 0,
    };

    ret = ioctl(iommufd, IOMMU_HWPT_ALLOC, &alloc_hwpt);
    trace_iommufd_backend_alloc_hwpt(iommufd, dev_id, pt_id, ret);

    if (ret) {
        error_report("IOMMU_HWPT_ALLOC failed: %s", strerror(errno));
    } else {
        *out_hwpt = alloc_hwpt.out_hwpt_id;
    }
    return !ret ? 0 : -errno;
}

static const TypeInfo iommufd_backend_info = {
    .name = TYPE_IOMMUFD_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(IOMMUFDBackend),
    .instance_init = iommufd_backend_init,
    .instance_finalize = iommufd_backend_finalize,
    .class_size = sizeof(IOMMUFDBackendClass),
    .class_init = iommufd_backend_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&iommufd_backend_info);
}

type_init(register_types);
