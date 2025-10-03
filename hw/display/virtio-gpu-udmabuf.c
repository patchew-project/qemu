/*
 * Virtio GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qemu/iov.h"
#include "ui/console.h"
#include "hw/virtio/virtio-gpu.h"
#include "hw/virtio/virtio-gpu-pixman.h"
#include "hw/vfio/vfio-device.h"
#include "trace.h"
#include "system/ramblock.h"
#include "system/hostmem.h"
#include <sys/ioctl.h>
#include <linux/memfd.h>
#include "qemu/memfd.h"
#include "standard-headers/linux/udmabuf.h"
#include "standard-headers/drm/drm_fourcc.h"

static void vfio_create_dmabuf(VFIODevice *vdev,
                               struct virtio_gpu_simple_resource *res)
{
#if defined(VIRTIO_GPU_VFIO_BLOB)
    res->dmabuf_fd = vfio_device_create_dmabuf(vdev, res->iov, res->iov_cnt);
    if (res->dmabuf_fd < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: VFIO_DEVICE_FEATURE_DMA_BUF: %s\n",
                      __func__, strerror(errno));
    }
#endif
}

static VFIODevice *vfio_device_lookup(MemoryRegion *mr)
{
#if defined(VIRTIO_GPU_VFIO_BLOB)
    VFIODevice *vdev;

    QLIST_FOREACH(vdev, &vfio_device_list, next) {
        if (vdev->dev == mr->dev) {
            return vdev;
        }
    }
#endif
    return NULL;
}

static void virtio_gpu_create_udmabuf(struct virtio_gpu_simple_resource *res)
{
    struct udmabuf_create_list *list;
    RAMBlock *rb;
    ram_addr_t offset;
    int udmabuf, i;

    udmabuf = udmabuf_fd();
    if (udmabuf < 0) {
        return;
    }

    list = g_malloc0(sizeof(struct udmabuf_create_list) +
                     sizeof(struct udmabuf_create_item) * res->iov_cnt);

    for (i = 0; i < res->iov_cnt; i++) {
        rcu_read_lock();
        rb = qemu_ram_block_from_host(res->iov[i].iov_base, false, &offset);
        rcu_read_unlock();

        if (!rb || rb->fd < 0) {
            g_free(list);
            return;
        }

        list->list[i].memfd  = rb->fd;
        list->list[i].offset = offset;
        list->list[i].size   = res->iov[i].iov_len;
    }

    list->count = res->iov_cnt;
    list->flags = UDMABUF_FLAGS_CLOEXEC;

    res->dmabuf_fd = ioctl(udmabuf, UDMABUF_CREATE_LIST, list);
    if (res->dmabuf_fd < 0) {
        warn_report("%s: UDMABUF_CREATE_LIST: %s", __func__,
                    strerror(errno));
    }
    g_free(list);
}

static void *vfio_dmabuf_mmap(struct virtio_gpu_simple_resource *res,
                              VFIODevice *vdev)
{
    struct vfio_region_info *info;
    ram_addr_t offset, len = 0;
    void *map, *submap;
    int i, ret = -1;
    RAMBlock *rb;

    /*
     * We first reserve a contiguous chunk of address space for the entire
     * dmabuf, then replace it with smaller mappings that correspond to the
     * individual segments of the dmabuf.
     */
    map = mmap(NULL, res->blob_size, PROT_READ, MAP_SHARED, vdev->fd, 0);
    if (map == MAP_FAILED) {
        return map;
    }

    for (i = 0; i < res->iov_cnt; i++) {
        rcu_read_lock();
        rb = qemu_ram_block_from_host(res->iov[i].iov_base, false, &offset);
        rcu_read_unlock();

        if (!rb) {
            goto err;
        }

#if defined(VIRTIO_GPU_VFIO_BLOB)
        ret = vfio_get_region_index_from_mr(rb->mr);
        if (ret < 0) {
            goto err;
        }

        ret = vfio_device_get_region_info(vdev, ret, &info);
#endif
        if (ret < 0) {
            goto err;
        }

        submap = mmap(map + len, res->iov[i].iov_len, PROT_READ,
                      MAP_SHARED | MAP_FIXED, vdev->fd,
                      info->offset + offset);
        if (submap == MAP_FAILED) {
            goto err;
        }

        len += res->iov[i].iov_len;
    }
    return map;
err:
    munmap(map, res->blob_size);
    return MAP_FAILED;
}

static void virtio_gpu_remap_udmabuf(struct virtio_gpu_simple_resource *res,
                                     VFIODevice *vdev)
{
    res->remapped = mmap(NULL, res->blob_size, PROT_READ,
                         MAP_SHARED, res->dmabuf_fd, 0);
    if (res->remapped == MAP_FAILED) {
        if (vdev) {
            res->remapped = vfio_dmabuf_mmap(res, vdev);
            if (res->remapped != MAP_FAILED) {
                return;
            }
        }
        warn_report("%s: dmabuf mmap failed: %s", __func__,
                    strerror(errno));
        res->remapped = NULL;
    }
}

static void virtio_gpu_destroy_udmabuf(struct virtio_gpu_simple_resource *res)
{
    if (res->remapped) {
        munmap(res->remapped, res->blob_size);
        res->remapped = NULL;
    }
    if (res->dmabuf_fd >= 0) {
        close(res->dmabuf_fd);
        res->dmabuf_fd = -1;
    }
}

static int find_memory_backend_type(Object *obj, void *opaque)
{
    bool *memfd_backend = opaque;
    int ret;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        HostMemoryBackend *backend = MEMORY_BACKEND(obj);
        RAMBlock *rb = backend->mr.ram_block;

        if (rb && rb->fd > 0) {
            ret = fcntl(rb->fd, F_GET_SEALS);
            if (ret > 0) {
                *memfd_backend = true;
            }
        }
    }

    return 0;
}

bool virtio_gpu_have_udmabuf(void)
{
    Object *memdev_root;
    int udmabuf;
    bool memfd_backend = false;

    udmabuf = udmabuf_fd();
    if (udmabuf < 0) {
        return false;
    }

    memdev_root = object_resolve_path("/objects", NULL);
    object_child_foreach(memdev_root, find_memory_backend_type, &memfd_backend);

    return memfd_backend;
}

void virtio_gpu_init_udmabuf(struct virtio_gpu_simple_resource *res)
{
    VFIODevice *vdev = NULL;
    void *pdata = NULL;
    ram_addr_t offset;
    RAMBlock *rb;

    res->dmabuf_fd = -1;
    if (res->iov_cnt == 1 &&
        res->iov[0].iov_len < 4096) {
        pdata = res->iov[0].iov_base;
    } else {
        rcu_read_lock();
        rb = qemu_ram_block_from_host(res->iov[0].iov_base, false, &offset);
        rcu_read_unlock();

        if (!rb) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Could not find ram block for host address\n",
                          __func__);
            return;
        }

        if (memory_region_is_ram_device(rb->mr)) {
            vdev = vfio_device_lookup(rb->mr);
            if (!vdev) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Could not find device to create dmabuf\n",
                              __func__);
                return;
            }

            vfio_create_dmabuf(vdev, res);
            if (res->dmabuf_fd < 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Could not create dmabuf from vfio device\n",
                              __func__);
                return;
            }
        } else if (memory_region_is_ram(rb->mr) && virtio_gpu_have_udmabuf()) {
            virtio_gpu_create_udmabuf(res);
            if (res->dmabuf_fd < 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Could not create dmabuf from memfd\n",
                              __func__);
                return;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: memory region cannot be used to create dmabuf\n",
                          __func__);
            return;
        }

        virtio_gpu_remap_udmabuf(res, vdev);
        if (!res->remapped) {
            return;
        }
        pdata = res->remapped;
    }

    res->blob = pdata;
}

void virtio_gpu_fini_udmabuf(struct virtio_gpu_simple_resource *res)
{
    virtio_gpu_destroy_udmabuf(res);
}

static void virtio_gpu_free_dmabuf(VirtIOGPU *g, VGPUDMABuf *dmabuf)
{
    struct virtio_gpu_scanout *scanout;

    scanout = &g->parent_obj.scanout[dmabuf->scanout_id];
    dpy_gl_release_dmabuf(scanout->con, dmabuf->buf);
    g_clear_pointer(&dmabuf->buf, qemu_dmabuf_free);
    QTAILQ_REMOVE(&g->dmabuf.bufs, dmabuf, next);
    g_free(dmabuf);
}

static VGPUDMABuf
*virtio_gpu_create_dmabuf(VirtIOGPU *g,
                          uint32_t scanout_id,
                          struct virtio_gpu_simple_resource *res,
                          struct virtio_gpu_framebuffer *fb,
                          struct virtio_gpu_rect *r)
{
    VGPUDMABuf *dmabuf;
    uint32_t offset = 0;

    if (res->dmabuf_fd < 0) {
        return NULL;
    }

    dmabuf = g_new0(VGPUDMABuf, 1);
    dmabuf->buf = qemu_dmabuf_new(r->width, r->height,
                                  &offset, &fb->stride,
                                  r->x, r->y, fb->width, fb->height,
                                  qemu_pixman_to_drm_format(fb->format),
                                  DRM_FORMAT_MOD_INVALID, &res->dmabuf_fd,
                                  1, true, false);
    dmabuf->scanout_id = scanout_id;
    QTAILQ_INSERT_HEAD(&g->dmabuf.bufs, dmabuf, next);

    return dmabuf;
}

int virtio_gpu_update_dmabuf(VirtIOGPU *g,
                             uint32_t scanout_id,
                             struct virtio_gpu_simple_resource *res,
                             struct virtio_gpu_framebuffer *fb,
                             struct virtio_gpu_rect *r)
{
    struct virtio_gpu_scanout *scanout = &g->parent_obj.scanout[scanout_id];
    VGPUDMABuf *new_primary, *old_primary = NULL;
    uint32_t width, height;

    new_primary = virtio_gpu_create_dmabuf(g, scanout_id, res, fb, r);
    if (!new_primary) {
        return -EINVAL;
    }

    if (g->dmabuf.primary[scanout_id]) {
        old_primary = g->dmabuf.primary[scanout_id];
    }

    width = qemu_dmabuf_get_width(new_primary->buf);
    height = qemu_dmabuf_get_height(new_primary->buf);
    g->dmabuf.primary[scanout_id] = new_primary;
    qemu_console_resize(scanout->con, width, height);
    dpy_gl_scanout_dmabuf(scanout->con, new_primary->buf);

    if (old_primary) {
        virtio_gpu_free_dmabuf(g, old_primary);
    }

    return 0;
}
