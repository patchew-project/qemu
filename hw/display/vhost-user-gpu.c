/*
 * vhost-user GPU Device
 *
 * Copyright Red Hat, Inc. 2018
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

#define VHOST_USER_GPU(obj)                                    \
    OBJECT_CHECK(VhostUserGPU, (obj), TYPE_VHOST_USER_GPU)

typedef enum VhostUserGpuRequest {
    VHOST_USER_GPU_NONE = 0,
    VHOST_USER_GPU_GET_PROTOCOL_FEATURES,
    VHOST_USER_GPU_SET_PROTOCOL_FEATURES,
    VHOST_USER_GPU_GET_DISPLAY_INFO,
    VHOST_USER_GPU_CURSOR_POS,
    VHOST_USER_GPU_CURSOR_POS_HIDE,
    VHOST_USER_GPU_CURSOR_UPDATE,
    VHOST_USER_GPU_SCANOUT,
    VHOST_USER_GPU_UPDATE,
    VHOST_USER_GPU_DMABUF_SCANOUT,
    VHOST_USER_GPU_DMABUF_UPDATE,
} VhostUserGpuRequest;

typedef struct VhostUserGpuDisplayInfoReply {
    struct virtio_gpu_resp_display_info info;
} VhostUserGpuDisplayInfoReply;

typedef struct VhostUserGpuCursorPos {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
} QEMU_PACKED VhostUserGpuCursorPos;

typedef struct VhostUserGpuCursorUpdate {
    VhostUserGpuCursorPos pos;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t data[64 * 64];
} QEMU_PACKED VhostUserGpuCursorUpdate;

typedef struct VhostUserGpuScanout {
    uint32_t scanout_id;
    uint32_t width;
    uint32_t height;
} QEMU_PACKED VhostUserGpuScanout;

typedef struct VhostUserGpuUpdate {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t data[];
} QEMU_PACKED VhostUserGpuUpdate;

typedef struct VhostUserGpuDMABUFScanout {
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
} QEMU_PACKED VhostUserGpuDMABUFScanout;

typedef struct VhostUserGpuMsg {
    uint32_t request; /* VhostUserGpuRequest */
    uint32_t size; /* the following payload size */
    union {
        VhostUserGpuCursorPos cursor_pos;
        VhostUserGpuCursorUpdate cursor_update;
        VhostUserGpuScanout scanout;
        VhostUserGpuUpdate update;
        VhostUserGpuDMABUFScanout dmabuf_scanout;
        uint64_t u64;
    } payload;
} QEMU_PACKED VhostUserGpuMsg;

static VhostUserGpuMsg m __attribute__ ((unused));
#define VHOST_USER_GPU_HDR_SIZE (sizeof(m.request) + sizeof(m.size))

static void vhost_user_gpu_update_blocked(VhostUserGPU *g, bool blocked);

static void
vhost_user_gpu_handle_cursor(VhostUserGPU *g, VhostUserGpuMsg *msg)
{
    VhostUserGpuCursorPos *pos = &msg->payload.cursor_pos;
    struct virtio_gpu_scanout *s;

    if (pos->scanout_id >= g->parent_obj.conf.max_outputs) {
        return;
    }
    s = &g->parent_obj.scanout[pos->scanout_id];

    if (msg->request == VHOST_USER_GPU_CURSOR_UPDATE) {
        VhostUserGpuCursorUpdate *up = &msg->payload.cursor_update;
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
                  msg->request != VHOST_USER_GPU_CURSOR_POS_HIDE);
}

static void
vhost_user_gpu_unblock(VhostUserGPU *g)
{
    uint32_t ok;

    qemu_chr_fe_write(&g->vhost_chr, (uint8_t *)&ok, sizeof(ok));

}
static void
vhost_user_gpu_handle_display(VhostUserGPU *g, VhostUserGpuMsg *msg)
{
    QemuConsole *con = NULL;
    struct virtio_gpu_scanout *s;

    switch (msg->request) {
    case VHOST_USER_GPU_GET_PROTOCOL_FEATURES: {
        uint64_t u64 = 0;
        qemu_chr_fe_write(&g->vhost_chr, (uint8_t *)&u64, sizeof(u64));
        break;
    }
    case VHOST_USER_GPU_SET_PROTOCOL_FEATURES: {
        break;
    }
    case VHOST_USER_GPU_GET_DISPLAY_INFO: {
        struct virtio_gpu_resp_display_info display_info = { 0, };
        display_info.hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
        virtio_gpu_base_fill_display_info(VIRTIO_GPU_BASE(g), &display_info);
        qemu_chr_fe_write(&g->vhost_chr,
                          (uint8_t *)&display_info, sizeof(display_info));
        break;
    }
    case VHOST_USER_GPU_SCANOUT: {
        VhostUserGpuScanout *m = &msg->payload.scanout;

        if (m->scanout_id >= g->parent_obj.conf.max_outputs) {
            return;
        }
        s = &g->parent_obj.scanout[m->scanout_id];
        con = s->con;

        s->ds = qemu_create_displaysurface(m->width, m->height);
        if (!s->ds) {
            return;
        }

        dpy_gfx_replace_surface(con, s->ds);
        break;
    }
    case VHOST_USER_GPU_DMABUF_SCANOUT: {
        VhostUserGpuDMABUFScanout *m = &msg->payload.dmabuf_scanout;
        int fd = qemu_chr_fe_get_msgfd(&g->vhost_chr);
        QemuDmaBuf *dmabuf;

        if (m->scanout_id >= g->parent_obj.conf.max_outputs) {
            if (fd >= 0) {
                close(fd);
            }
            break;
        }

        con = g->parent_obj.scanout[m->scanout_id].con;
        dmabuf = &g->dmabuf[m->scanout_id];
        if (g->parent_obj.enable && dmabuf->fd >= 0) {
            close(dmabuf->fd);
            dmabuf->fd = -1;
        }
        dpy_gl_release_dmabuf(con, dmabuf);
        if (fd == -1) {
            dpy_gl_scanout_disable(con);
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
        dpy_gl_scanout_dmabuf(con, dmabuf);
        break;
    }
    case VHOST_USER_GPU_DMABUF_UPDATE: {
        VhostUserGpuUpdate *m = &msg->payload.update;

        if (m->scanout_id >= g->parent_obj.conf.max_outputs ||
            !g->parent_obj.scanout[m->scanout_id].con) {
            error_report("invalid scanout update: %d", m->scanout_id);
            vhost_user_gpu_unblock(g);
            break;
        }

        con = g->parent_obj.scanout[m->scanout_id].con;
        dpy_gl_update(con, m->x, m->y, m->width, m->height);
        g->backend_blocked = true;
        break;
    }
    case VHOST_USER_GPU_UPDATE: {
        VhostUserGpuUpdate *m = &msg->payload.update;

        if (m->scanout_id >= g->parent_obj.conf.max_outputs) {
            break;
        }
        s = &g->parent_obj.scanout[m->scanout_id];
        con = s->con;
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
        dpy_gfx_update(con, m->x, m->y, m->width, m->height);
        break;
    }
    default:
        g_warning("unhandled message %d %d", msg->request, msg->size);
    }

    if (con && qemu_console_is_gl_blocked(con)) {
        vhost_user_gpu_update_blocked(g, true);
    }
}

static void
vhost_user_gpu_chr_read(void *opaque)
{
    VhostUserGPU *g = opaque;
    VhostUserGpuMsg *msg = NULL;
    VhostUserGpuRequest request;
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

    msg = g_malloc(VHOST_USER_GPU_HDR_SIZE + size);
    g_return_if_fail(msg != NULL);

    r = qemu_chr_fe_read_all(&g->vhost_chr,
                             (uint8_t *)&msg->payload, size);
    if (r != size) {
        error_report("failed to read msg payload %d != %d", r, size);
        goto end;
    }

    msg->request = request;
    msg->size = size;

    if (request == VHOST_USER_GPU_CURSOR_UPDATE ||
        request == VHOST_USER_GPU_CURSOR_POS ||
        request == VHOST_USER_GPU_CURSOR_POS_HIDE) {
        vhost_user_gpu_handle_cursor(g, msg);
    } else {
        vhost_user_gpu_handle_display(g, msg);
    }

end:
    g_free(msg);
}

static void
vhost_user_gpu_update_blocked(VhostUserGPU *g, bool blocked)
{
    qemu_set_fd_handler(g->vhost_gpu_fd,
                        blocked ? NULL : vhost_user_gpu_chr_read, NULL, g);
}

static void
vhost_user_gpu_gl_unblock(VirtIOGPUBase *b)
{
    VhostUserGPU *g = VHOST_USER_GPU(b);

    if (g->backend_blocked) {
        vhost_user_gpu_unblock(VHOST_USER_GPU(g));
        g->backend_blocked = false;
    }

    vhost_user_gpu_update_blocked(VHOST_USER_GPU(g), false);
}

static void
vhost_user_gpu_set_status(VirtIODevice *vdev, uint8_t val)
{
    VhostUserGPU *g = VHOST_USER_GPU(vdev);

    if (val & VIRTIO_CONFIG_S_DRIVER_OK && vdev->vm_running) {
        vhost_user_backend_start(g->vhost);
    } else {
        /* TODO: forcefully gl unblock ? */
        vhost_user_backend_stop(g->vhost);
    }
}

static bool
vhost_user_gpu_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VhostUserGPU *g = VHOST_USER_GPU(vdev);

    return vhost_virtqueue_pending(&g->vhost->dev, idx);
}

static void
vhost_user_gpu_guest_notifier_mask(VirtIODevice *vdev, int idx, bool mask)
{
    VhostUserGPU *g = VHOST_USER_GPU(vdev);

    vhost_virtqueue_mask(&g->vhost->dev, vdev, idx, mask);
}

static void
vhost_user_gpu_is_busy(const Object *obj, const char *name,
                       Object *val, Error **errp)
{
    VhostUserGPU *g = VHOST_USER_GPU(obj);

    if (g->vhost) {
        error_setg(errp, "can't use already busy vhost-user");
    } else {
        qdev_prop_allow_set_link_before_realize(obj, name, val, errp);
    }
}

static void
vhost_user_gpu_instance_init(Object *obj)
{
    VhostUserGPU *g = VHOST_USER_GPU(obj);

    object_property_add_link(obj, "vhost-user", TYPE_VHOST_USER_BACKEND,
                             (Object **)&g->vhost,
                             vhost_user_gpu_is_busy,
                             OBJ_PROP_LINK_STRONG,
                             &error_abort);
}

static void
vhost_user_gpu_reset(VirtIODevice *vdev)
{
    VhostUserGPU *g = VHOST_USER_GPU(vdev);

    virtio_gpu_base_reset(VIRTIO_GPU_BASE(vdev));

    vhost_user_backend_stop(g->vhost);
}

static void
vhost_user_gpu_device_realize(DeviceState *qdev, Error **errp)
{
    VhostUserGPU *g = VHOST_USER_GPU(qdev);
    VirtIODevice *vdev = VIRTIO_DEVICE(g);
    Chardev *chr;
    int sv[2];
    uint32_t num_capsets;

    if (!g->vhost) {
        error_setg(errp, "'vhost-user' property is required");
        return;
    }

    if (vhost_user_backend_dev_init(g->vhost, vdev, 2, errp) < 0) {
        return;
    }

    if (virtio_has_feature(g->vhost->dev.features, VIRTIO_GPU_F_VIRGL)) {
        g->parent_obj.conf.flags |= 1 << VIRTIO_GPU_FLAG_VIRGL_ENABLED;
    }

    if (vhost_user_gpu_get_num_capsets(&g->vhost->dev, &num_capsets) < 0) {
        error_setg(errp, "Failed to get num-capsets");
        return;
    }

    if (!virtio_gpu_base_device_realize(qdev, num_capsets, NULL, NULL, errp)) {
        return;
    }

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        error_setg_errno(errp, errno, "socketpair() failed");
        return;
    }

    chr = CHARDEV(object_new(TYPE_CHARDEV_SOCKET));
    if (!chr || qemu_chr_add_client(chr, sv[0]) == -1) {
        error_setg(errp, "Failed to make socket chardev");
        goto err;
    }
    if (!qemu_chr_fe_init(&g->vhost_chr, chr, errp)) {
        goto err;
    }
    if (vhost_user_gpu_set_socket(&g->vhost->dev, sv[1]) < 0) {
        error_setg(errp, "Failed to set vhost-user-gpu socket");
        goto err;
    }

    g->vhost_gpu_fd = sv[0];
    vhost_user_gpu_update_blocked(g, false);
    close(sv[1]);
    return;

err:
    close(sv[0]);
    close(sv[1]);
    if (chr) {
        object_unref(OBJECT(chr));
    }
}

static Property vhost_user_gpu_properties[] = {
    VIRTIO_GPU_BASE_PROPERTIES(VhostUserGPU, parent_obj.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void
vhost_user_gpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    VirtIOGPUBaseClass *vgc = VIRTIO_GPU_BASE_CLASS(klass);

    vgc->gl_unblock = vhost_user_gpu_gl_unblock;

    vdc->realize = vhost_user_gpu_device_realize;
    vdc->reset = vhost_user_gpu_reset;
    vdc->set_status   = vhost_user_gpu_set_status;
    vdc->guest_notifier_mask = vhost_user_gpu_guest_notifier_mask;
    vdc->guest_notifier_pending = vhost_user_gpu_guest_notifier_pending;
    vdc->reset = vhost_user_gpu_reset;

    dc->props = vhost_user_gpu_properties;
}

static const TypeInfo vhost_user_gpu_info = {
    .name = TYPE_VHOST_USER_GPU,
    .parent = TYPE_VIRTIO_GPU_BASE,
    .instance_size = sizeof(VhostUserGPU),
    .instance_init = vhost_user_gpu_instance_init,
    .class_init = vhost_user_gpu_class_init,
};

static void vhost_user_gpu_register_types(void)
{
    type_register_static(&vhost_user_gpu_info);
}

type_init(vhost_user_gpu_register_types)
