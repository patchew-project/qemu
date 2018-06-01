/*
 * Virtio vhost-user GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2018
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *     Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef VUGPU_H_
#define VUGPU_H_

#include "contrib/libvhost-user/libvhost-user-glib.h"
#include "standard-headers/linux/virtio_gpu.h"

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu/iov.h"
#include "qemu/bswap.h"

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

typedef struct VhostGpuUpdate {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t data[];
} QEMU_PACKED VhostGpuUpdate;

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

struct virtio_gpu_scanout {
    uint32_t width, height;
    int x, y;
    int invalidate;
    uint32_t resource_id;
};

typedef struct VuGpu {
    VugDev dev;
    int sock_fd;
    GSource *renderer_source;

    bool virgl;
    bool virgl_inited;
    uint32_t inflight;

    struct virtio_gpu_scanout scanout[VIRTIO_GPU_MAX_SCANOUTS];
    QTAILQ_HEAD(, virtio_gpu_simple_resource) reslist;
    QTAILQ_HEAD(, virtio_gpu_ctrl_command) fenceq;
} VuGpu;

struct virtio_gpu_ctrl_command {
    VuVirtqElement elem;
    VuVirtq *vq;
    struct virtio_gpu_ctrl_hdr cmd_hdr;
    uint32_t error;
    bool finished;
    QTAILQ_ENTRY(virtio_gpu_ctrl_command) next;
};

#define VUGPU_FILL_CMD(out) do {                                \
        size_t s;                                               \
        s = iov_to_buf(cmd->elem.out_sg, cmd->elem.out_num, 0,  \
                       &out, sizeof(out));                      \
        if (s != sizeof(out)) {                                 \
            g_critical("%s: command size incorrect %zu vs %zu", \
                       __func__, s, sizeof(out));               \
            return;                                             \
        }                                                       \
    } while (0)


void    vg_ctrl_response(VuGpu *g,
                         struct virtio_gpu_ctrl_command *cmd,
                         struct virtio_gpu_ctrl_hdr *resp,
                         size_t resp_len);

void    vg_ctrl_response_nodata(VuGpu *g,
                                struct virtio_gpu_ctrl_command *cmd,
                                enum virtio_gpu_ctrl_type type);

int     vg_create_mapping_iov(VuGpu *g,
                              struct virtio_gpu_resource_attach_backing *ab,
                              struct virtio_gpu_ctrl_command *cmd,
                              struct iovec **iov);

void    vg_get_display_info(VuGpu *vg, struct virtio_gpu_ctrl_command *cmd);

ssize_t vg_sock_fd_write(int sock, void *buf, ssize_t buflen, int fd);

#endif
