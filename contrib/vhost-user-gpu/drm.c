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

static bool
mem_alloc_bo(struct drm_buffer *buf)
{
    buf->mmap = g_malloc(buf->width * buf->height * buf->bpp / 8);
    buf->stride = buf->width * buf->bpp / 8;
    return true;
}

static void
mem_free_bo(struct drm_buffer *buf)
{
    g_free(buf->mmap);
}

static bool
mem_map_bo(struct drm_buffer *buf)
{
    return buf->mmap != NULL;
}

static void
mem_unmap_bo(struct drm_buffer *buf)
{
}

static void
mem_device_destroy(struct drm_device *dev)
{
}

#ifdef CONFIG_MEMFD
struct udmabuf_create {
        uint32_t memfd;
        uint32_t flags;
        uint64_t offset;
        uint64_t size;
};

#define UDMABUF_CREATE _IOW('u', 0x42, struct udmabuf_create)

static size_t
udmabuf_get_size(struct drm_buffer *buf)
{
    return ROUND_UP(buf->width * buf->height * buf->bpp / 8, getpagesize());
}

static bool
udmabuf_alloc_bo(struct drm_buffer *buf)
{
    int ret;

    buf->memfd = memfd_create("udmabuf-bo", MFD_ALLOW_SEALING);
    if (buf->memfd < 0) {
        return false;
    }

    ret = ftruncate(buf->memfd, udmabuf_get_size(buf));
    if (ret < 0) {
        close(buf->memfd);
        return false;
    }

    ret = fcntl(buf->memfd, F_ADD_SEALS, F_SEAL_SHRINK);
    if (ret < 0) {
        close(buf->memfd);
        return false;
    }

    buf->stride = buf->width * buf->bpp / 8;

    return true;
}

static void
udmabuf_free_bo(struct drm_buffer *buf)
{
    close(buf->memfd);
}

static bool
udmabuf_map_bo(struct drm_buffer *buf)
{
    buf->mmap = mmap(NULL, udmabuf_get_size(buf),
                     PROT_READ | PROT_WRITE, MAP_SHARED, buf->memfd, 0);
    if (buf->mmap == MAP_FAILED) {
        return false;
    }

    return true;
}

static bool
udmabuf_export_bo_to_prime(struct drm_buffer *buf, int *fd)
{
    struct udmabuf_create create = {
        .memfd = buf->memfd,
        .offset = 0,
        .size = udmabuf_get_size(buf),
    };

    *fd = ioctl(buf->dev->fd, UDMABUF_CREATE, &create);

    return *fd >= 0;
}

static void
udmabuf_unmap_bo(struct drm_buffer *buf)
{
    munmap(buf->mmap, udmabuf_get_size(buf));
}

static void
udmabuf_device_destroy(struct drm_device *dev)
{
    close(dev->fd);
}
#endif

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
    g_free(dev->name);
}

bool
drm_device_init(struct drm_device *dev, int fd)
{
    drmVersionPtr version = drmGetVersion(fd);

    dev->fd = fd;
    dev->name = g_strdup(version->name);
    drmFreeVersion(version);

    if (0) {
        /* nothing */
    }
#ifdef CONFIG_LIBDRM_INTEL
    else if (g_str_equal(dev->name, "i915")) {
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
#ifdef CONFIG_MEMFD
    else if (g_file_test("/dev/udmabuf", G_FILE_TEST_EXISTS)) {
        dev->fd = open("/dev/udmabuf", O_RDWR);
        if (dev->fd < 0) {
            return false;
        }
        g_debug("Using experimental udmabuf backend");
        dev->alloc_bo = udmabuf_alloc_bo;
        dev->free_bo = udmabuf_free_bo;
        dev->export_bo_to_prime = udmabuf_export_bo_to_prime;
        dev->map_bo = udmabuf_map_bo;
        dev->unmap_bo = udmabuf_unmap_bo;
        dev->device_destroy = udmabuf_device_destroy;
    }
#endif
    else {
        g_debug("Using mem fallback");
        dev->alloc_bo = mem_alloc_bo;
        dev->free_bo = mem_free_bo;
        dev->map_bo = mem_map_bo;
        dev->unmap_bo = mem_unmap_bo;
        dev->device_destroy = mem_device_destroy;
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
drm_buffer_can_get_dmabuf_fd(struct drm_buffer *buffer)
{
    if (!buffer->dev->export_bo_to_prime) {
        return false;
    }

    return true;
}

bool
drm_buffer_get_dmabuf_fd(struct drm_buffer *buffer, int *fd)
{
    if (!drm_buffer_can_get_dmabuf_fd(buffer) ||
        !buffer->dev->export_bo_to_prime(buffer, fd)) {
        g_warning("Failed to get dmabuf");
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
    buffer->stride = 0; /* modified during alloc */
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
