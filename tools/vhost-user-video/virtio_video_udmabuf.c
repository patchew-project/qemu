// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtio Video Device
 *
 * Copyright Red Hat, Inc. 2023
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "linux/udmabuf.h"

#include "vuvideo.h"

static size_t
udmabuf_get_size(struct VuVideoDMABuf *buf)
{
    return ROUND_UP(buf->length, qemu_real_host_page_size());
}

static bool
udmabuf_alloc_bm(struct VuVideoDMABuf *buf)
{
    int ret;

    buf->memfd = memfd_create("udmabuf-video-bm", MFD_ALLOW_SEALING);
    if (buf->memfd < 0) {
        g_printerr("%s: memfd_create failed.", __func__);
        return false;
    }

    ret = ftruncate(buf->memfd, udmabuf_get_size(buf));
    if (ret < 0) {
        g_printerr("%s: ftruncate failed.", __func__);
        close(buf->memfd);
        return false;
    }

    ret = fcntl(buf->memfd, F_ADD_SEALS, F_SEAL_SHRINK);
    if (ret < 0) {
        g_printerr("%s: fcntl failed.", __func__);
        close(buf->memfd);
        return false;
    }

    return true;
}

static void
udmabuf_free_bm(struct VuVideoDMABuf *buf)
{
    close(buf->memfd);
}

static bool
udmabuf_map_bm(struct VuVideoDMABuf *buf)
{
    g_debug("Map the buffer memory.");
    buf->start = mmap(NULL, udmabuf_get_size(buf),
                      PROT_READ | PROT_WRITE, MAP_SHARED, buf->memfd, 0);
    if (buf->start == MAP_FAILED) {
        return false;
    }

    return true;
}

static void
udmabuf_unmap_bm(struct VuVideoDMABuf *buf)
{
    g_debug("Unmap the buffer memory.");
    munmap(buf->start, udmabuf_get_size(buf));
}

static int
udmabuf_get_fd(struct VuVideoDMABuf *buf)
{
    if (buf->dmafd > 0) {
        return buf->dmafd;
    }

    struct udmabuf_create create = {
        .memfd = buf->memfd,
        .offset = 0,
        .size = udmabuf_get_size(buf),
    };

    buf->dmafd = ioctl(buf->dev->fd, UDMABUF_CREATE, &create);
    if (buf->dmafd < 0) {
        g_printerr("%s: UDMABUF_CREATE failed.", __func__);
    }

    return buf->dmafd;
}

static void
udmabuf_device_destroy(struct vuvbm_device *dev)
{
    close(dev->fd);
}

static bool
vuvbm_buffer_map(struct VuVideoDMABuf *buf)
{
    struct vuvbm_device *dev = buf->dev;

    return dev->map_bm(buf);
}

bool vuvbm_buffer_create(struct vuvbm_device *dev,
                         struct VuVideoDMABuf *buffer, uint32_t len)
{
    g_debug("Creating buffer length(%d)", len);
    buffer->dev = dev;
    buffer->length = len;
    if (!dev->alloc_bm(buffer)) {
        g_warning("alloc_bm failed");
        return false;
    }

    if (!vuvbm_buffer_map(buffer)) {
        g_warning("map_bm failed");
        goto err;
    }

    return true;

err:
    buffer->dev->free_bm(buffer);
    return false;
}

void vuvbm_init_device(struct vuvbm_device *dev)
{
    if (!dev->opened && g_file_test("/dev/udmabuf", G_FILE_TEST_EXISTS)) {
        dev->fd = open("/dev/udmabuf", O_RDWR);
        if (dev->fd >= 0) {
            g_debug("Using experimental udmabuf backend");
            dev->alloc_bm = udmabuf_alloc_bm;
            dev->free_bm = udmabuf_free_bm;
            dev->get_fd = udmabuf_get_fd;
            dev->map_bm = udmabuf_map_bm;
            dev->unmap_bm = udmabuf_unmap_bm;
            dev->device_destroy = udmabuf_device_destroy;
            dev->resource_uuids = g_hash_table_new_full(
                NULL, NULL, NULL, g_free);
            dev->opened = true;
        }
    }
    g_debug("Using udmabuf backend");
}

struct VuVideoDMABuf *vuvbm_lookup(struct vuvbm_device *dev, QemuUUID uuid)
{
    g_debug("Lookup for buffer uuid(%s)", qemu_uuid_unparse_strdup(&uuid));
    return g_hash_table_lookup(dev->resource_uuids, &uuid);
}

void vuvbm_buffer_destroy(struct VuVideoDMABuf *buffer)
{
    struct vuvbm_device *dev = buffer->dev;

    dev->unmap_bm(buffer);
    dev->free_bm(buffer);
}

void vuvbm_device_destroy(struct vuvbm_device *dev)
{
    if (!dev->opened) {
        return;
    }

    dev->device_destroy(dev);
}
