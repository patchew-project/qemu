/*
 * Virtio vhost-user GPU Device
 *
 * DRM helpers
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef VHOST_USER_GPU_DRM_H
#define VHOST_USER_GPU_DRM_H

#include "qemu/osdep.h"

#ifdef CONFIG_LIBDRM_INTEL
#include <i915_drm.h>
#include <intel_bufmgr.h>
#endif

struct drm_buffer;

struct drm_device {
    bool inited;
    int fd;
    char *name;
#ifdef CONFIG_LIBDRM_INTEL
    drm_intel_bufmgr *bufmgr;
#endif

    bool (*alloc_bo)(struct drm_buffer *buf);
    void (*free_bo)(struct drm_buffer *buf);
    bool (*export_bo_to_prime)(struct drm_buffer *buf, int *fd);
    bool (*map_bo)(struct drm_buffer *buf);
    void (*unmap_bo)(struct drm_buffer *buf);
    void (*device_destroy)(struct drm_device *dev);
};

struct drm_buffer {
    struct drm_device *dev;

#ifdef CONFIG_LIBDRM_INTEL
    drm_intel_bo *intel_bo;
#endif /* HAVE_LIBDRM_INTEL */

    uint32_t gem_handle;
    int dmabuf_fd;
    uint8_t *mmap;

    int width;
    int height;
    int bpp;
    unsigned long stride;
    int format;
};

bool drm_device_init(struct drm_device *dev, int fd);
void drm_device_destroy(struct drm_device *dev);

bool drm_buffer_create(struct drm_buffer *buffer, struct drm_device *dev,
                       int width, int height);
bool drm_buffer_get_dmabuf_fd(struct drm_buffer *buffer, int *fd);
void drm_buffer_destroy(struct drm_buffer *buffer);

#endif
