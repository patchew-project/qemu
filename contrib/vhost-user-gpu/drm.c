/*
 * Virtio vhost-user GPU Device
 *
 * DRM helpers
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <xf86drm.h> /* not much to do with xf86 */
#include <drm_fourcc.h>

#include "drm.h"

#ifdef CONFIG_LIBDRM_INTEL
static bool
intel_alloc_bo(struct drm_buffer *buf)
{
    uint32_t tiling = I915_TILING_NONE;

    buf->intel_bo = drm_intel_bo_alloc_tiled(buf->dev->bufmgr, "vhost-user-gpu",
                                             buf->width, buf->height,
                                             (buf->bpp / 8), &tiling,
                                             &buf->stride, 0);

    if (!buf->intel_bo) {
        return false;
    }

    if (tiling != I915_TILING_NONE) {
        drm_intel_bo_unreference(buf->intel_bo);
        return false;
    }

    return true;
}

static void
intel_free_bo(struct drm_buffer *buf)
{
    drm_intel_bo_unreference(buf->intel_bo);
}

static bool
intel_map_bo(struct drm_buffer *buf)
{
    if (drm_intel_gem_bo_map_gtt(buf->intel_bo) != 0) {
        return false;
    }

    buf->mmap = buf->intel_bo->virtual;

    return true;
}

static bool
intel_export_bo_to_prime(struct drm_buffer *buffer, int *fd)
{
    if (drm_intel_bo_gem_export_to_prime(buffer->intel_bo, fd) < 0) {
        return false;
    }

    return true;
}

static void
intel_unmap_bo(struct drm_buffer *buf)
{
    drm_intel_gem_bo_unmap_gtt(buf->intel_bo);
}

static void
intel_device_destroy(struct drm_device *dev)
{
    drm_intel_bufmgr_destroy(dev->bufmgr);
}

#endif /* CONFIG_LIBDRM_INTEL */

void
drm_device_destroy(struct drm_device *dev)
{
    if (!dev->inited) {
        return;
    }

    dev->device_destroy(dev);
}

bool
drm_device_init(struct drm_device *dev, int fd)
{
    drmVersionPtr version = drmGetVersion(fd);

    dev->fd = fd;
    dev->name = strdup(version->name);

    if (0) {
        /* nothing */
    }
#ifdef CONFIG_LIBDRM_INTEL
    else if (!strcmp(dev->name, "i915")) {
        dev->bufmgr = drm_intel_bufmgr_gem_init(fd, 32);
        if (!dev->bufmgr) {
            return false;
        }
        dev->alloc_bo = intel_alloc_bo;
        dev->free_bo = intel_free_bo;
        dev->export_bo_to_prime = intel_export_bo_to_prime;
        dev->map_bo = intel_map_bo;
        dev->unmap_bo = intel_unmap_bo;
        dev->device_destroy = intel_device_destroy;
    }
#endif
    else {
        g_warning("Error: drm device %s unsupported.", dev->name);
        return false;
    }

    dev->inited = true;
    return true;
}

static bool
drm_buffer_map(struct drm_buffer *buf)
{
    struct drm_device *dev = buf->dev;

    return dev->map_bo(buf);
}

static void
drm_buffer_unmap(struct drm_buffer *buf)
{
    struct drm_device *dev = buf->dev;

    dev->unmap_bo(buf);
}

bool
drm_buffer_get_dmabuf_fd(struct drm_buffer *buffer, int *fd)
{
    if (!buffer->dev->export_bo_to_prime(buffer, fd)) {
        g_warning("gem_export_to_prime failed");
        return false;
    }

    if (*fd < 0) {
        g_warning("error: dmabuf_fd < 0");
        return false;
    }

    return true;
}

bool
drm_buffer_create(struct drm_buffer *buffer, struct drm_device *dev,
                  int width, int height)
{
    buffer->dev = dev;
    buffer->width = width;
    buffer->height = height;
    buffer->bpp = 32;
    buffer->format = DRM_FORMAT_XRGB8888;
    if (!dev->alloc_bo(buffer)) {
        g_warning("alloc_bo failed");
        return false;
    }

    if (!drm_buffer_map(buffer)) {
        g_warning("map_bo failed");
        goto err;
    }

    return true;

err:
    dev->free_bo(buffer);
    return false;
}

void
drm_buffer_destroy(struct drm_buffer *buffer)
{
    struct drm_device *drm_dev = buffer->dev;

    drm_buffer_unmap(buffer);
    drm_dev->free_bo(buffer);
}
