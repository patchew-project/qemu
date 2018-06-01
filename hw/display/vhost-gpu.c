/*
 * Virtio vhost GPU Device
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *     Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-gpu.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"

typedef enum VhostGpuRequest {
    VHOST_GPU_NONE = 0,
    VHOST_GPU_CURSOR_POS,
    VHOST_GPU_CURSOR_POS_HIDE,
    VHOST_GPU_CURSOR_UPDATE,
    VHOST_GPU_SCANOUT,
    VHOST_GPU_UPDATE,
    VHOST_GPU_GL_SCANOUT,
    VHOST_GPU_GL_UPDATE,
} VhostGpuRequest;

typedef struct VhostGpuCursorPos {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
} QEMU_PACKED VhostGpuCursorPos;

typedef struct VhostGpuCursorUpdate {
    VhostGpuCursorPos pos;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t data[64 * 64];
} QEMU_PACKED VhostGpuCursorUpdate;

typedef struct VhostGpuScanout {
    uint32_t scanout_id;
    uint32_t width;
    uint32_t height;
} QEMU_PACKED VhostGpuScanout;

typedef struct VhostGpuGlScanout {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t fd_width;
    uint32_t fd_height;
    uint32_t fd_stride;
    uint32_t fd_flags;
    int fd_drm_fourcc;
} QEMU_PACKED VhostGpuGlScanout;

typedef struct VhostGpuUpdate {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t data[];
} QEMU_PACKED VhostGpuUpdate;

typedef struct VhostGpuMsg {
    VhostGpuRequest request;
    uint32_t size; /* the following payload size */
    union {
        VhostGpuCursorPos cursor_pos;
        VhostGpuCursorUpdate cursor_update;
        VhostGpuScanout scanout;
        VhostGpuUpdate update;
        VhostGpuGlScanout gl_scanout;
    } payload;
} QEMU_PACKED VhostGpuMsg;

static VhostGpuMsg m __attribute__ ((unused));
#define VHOST_GPU_HDR_SIZE (sizeof(m.request) + sizeof(m.size))

static void vhost_gpu_handle_cursor(VirtIOGPU *g, VhostGpuMsg *msg)
{
    VhostGpuCursorPos *pos = &msg->payload.cursor_pos;
    struct virtio_gpu_scanout *s;

    if (pos->scanout_id >= g->conf.max_outputs) {
        return;
    }
    s = &g->scanout[pos->scanout_id];

    if (msg->request == VHOST_GPU_CURSOR_UPDATE) {
        VhostGpuCursorUpdate *up = &msg->payload.cursor_update;
        if (!s->current_cursor) {
            s->current_cursor = cursor_alloc(64, 64);
        }

        s->current_cursor->hot_x = up->hot_x;
        s->current_cursor->hot_y = up->hot_y;

        memcpy(s->current_cursor->data, up->data,
               64 * 64 * sizeof(uint32_t));

        dpy_cursor_define(s->con, s->current_cursor);
    }

    dpy_mouse_set(s->con, pos->x, pos->y,
                  msg->request != VHOST_GPU_CURSOR_POS_HIDE);
}

static void vhost_gpu_handle_display(VirtIOGPU *g, VhostGpuMsg *msg)
{
    struct virtio_gpu_scanout *s;

    switch (msg->request) {
    case VHOST_GPU_SCANOUT: {
        VhostGpuScanout *m = &msg->payload.scanout;

        if (m->scanout_id >= g->conf.max_outputs) {
            return;
        }
        s = &g->scanout[m->scanout_id];

        s->ds = qemu_create_displaysurface(m->width, m->height);
        if (!s->ds) {
            return;
        }

        dpy_gfx_replace_surface(s->con, s->ds);
        break;
    }
    case VHOST_GPU_GL_SCANOUT: {
        VhostGpuGlScanout *m = &msg->payload.gl_scanout;
        int fd = qemu_chr_fe_get_msgfd(&g->vhost_chr);
        QemuDmaBuf *dmabuf;

        if (m->scanout_id >= g->conf.max_outputs) {
            if (fd >= 0) {
                close(fd);
            }
            break;
        }

        dmabuf = &g->dmabuf[m->scanout_id];
        if (g->enable && dmabuf->fd >= 0) {
            close(dmabuf->fd);
            dmabuf->fd = -1;
        }
        if (fd == -1) {
            dpy_gl_release_dmabuf(g->scanout[m->scanout_id].con, dmabuf);
            break;
        }
        *dmabuf = (QemuDmaBuf) {
            .fd = fd,
            .width = m->fd_width,
            .height = m->fd_height,
            .stride = m->fd_stride,
            .fourcc = m->fd_drm_fourcc,
            .y0_top = m->fd_flags & VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP,
        };
        g->enable = 1;
        dpy_gl_scanout_dmabuf(g->scanout[m->scanout_id].con, dmabuf);
        break;
    }
    case VHOST_GPU_GL_UPDATE: {
        VhostGpuUpdate *m = &msg->payload.update;
        QemuConsole *con = g->scanout[m->scanout_id].con;

        if (m->scanout_id >= g->conf.max_outputs ||
            !g->scanout[m->scanout_id].con) {
            break;
        }

        graphic_hw_gl_block(con, true);
        dpy_gl_update(con, m->x, m->y, m->width, m->height);
        graphic_hw_gl_block(con, false);
        break;
    }
    case VHOST_GPU_UPDATE: {
        VhostGpuUpdate *m = &msg->payload.update;

        if (m->scanout_id >= g->conf.max_outputs) {
            break;
        }
        s = &g->scanout[m->scanout_id];

        pixman_image_t *image =
            pixman_image_create_bits(PIXMAN_x8r8g8b8,
                                     m->width,
                                     m->height,
                                     (uint32_t *)m->data,
                                     m->width * 4);

        pixman_image_composite(PIXMAN_OP_SRC,
                               image, NULL, s->ds->image,
                               0, 0, 0, 0, m->x, m->y, m->width, m->height);

        pixman_image_unref(image);
        dpy_gfx_update(s->con, m->x, m->y, m->width, m->height);
        break;
    }
    default:
        g_warning("unhandled message %d %d", msg->request, msg->size);
    }
}

static void vhost_gpu_chr_read(void *opaque)
{
    VirtIOGPU *g = opaque;
    VhostGpuMsg *msg = NULL;
    VhostGpuRequest request;
    uint32_t size;
    int r;

    r = qemu_chr_fe_read_all(&g->vhost_chr,
                             (uint8_t *)&request, sizeof(uint32_t));
    if (r != sizeof(uint32_t)) {
        error_report("failed to read msg header: %d, %d", r, errno);
        goto end;
    }

    r = qemu_chr_fe_read_all(&g->vhost_chr,
                             (uint8_t *)&size, sizeof(uint32_t));
    if (r != sizeof(uint32_t)) {
        error_report("failed to read msg size");
        goto end;
    }

    msg = g_malloc(VHOST_GPU_HDR_SIZE + size);
    g_return_if_fail(msg != NULL);

    r = qemu_chr_fe_read_all(&g->vhost_chr,
                             (uint8_t *)&msg->payload, size);
    if (r != size) {
        error_report("failed to read msg payload %d != %d", r, size);
        goto end;
    }

    msg->request = request;
    msg->size = size;

    if (request == VHOST_GPU_CURSOR_UPDATE ||
        request == VHOST_GPU_CURSOR_POS ||
        request == VHOST_GPU_CURSOR_POS_HIDE) {
        vhost_gpu_handle_cursor(g, msg);
    } else {
        vhost_gpu_handle_display(g, msg);
    }

end:
    g_free(msg);
}

int vhost_gpu_init(VirtIOGPU *g, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(g);
    Chardev *chr;
    int sv[2];

    if (vhost_user_backend_dev_init(g->vhost, vdev, 2, errp) < 0) {
        return -1;
    }

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        error_setg_errno(errp, errno, "socketpair() failed");
        return -1;
    }

    chr = CHARDEV(object_new(TYPE_CHARDEV_SOCKET));
    if (!chr || qemu_chr_add_client(chr, sv[0]) == -1) {
        error_setg(errp, "Failed to make socket chardev");
        object_unref(OBJECT(chr));
        return -1;
    }
    if (!qemu_chr_fe_init(&g->vhost_chr, chr, errp)) {
        object_unref(OBJECT(chr));
        return -1;
    }

    qemu_set_fd_handler(sv[0], vhost_gpu_chr_read, NULL, g);

    vhost_user_gpu_set_socket(&g->vhost->dev, sv[1]);

    close(sv[1]);

    return 0;
}
