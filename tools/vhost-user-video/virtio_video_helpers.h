// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-video helpers
 *
 * Copyright Red Hat, Inc. 2023
 * Copyright Linaro 2021
 *
 * Authors:
 *      Peter Griffin <peter.griffin@linaro.org>
 *      Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VIRTIO_VIDEO_HELPERS_H
#define VIRTIO_VIDEO_HELPERS_H

#include <stdint.h>
#include "standard-headers/linux/virtio_video.h"
#include <linux/videodev2.h>
#include "libvhost-user-glib.h"
#include "libvhost-user.h"
#include "qemu/uuid.h"
#include "qemu/queue.h"

/*
 * Structure to track internal state of VIDEO Device
 */

struct resource;
struct VuVideoDMABuf;

struct vuvbm_device {
    bool opened;
    int fd;

    bool (*alloc_bm)(struct VuVideoDMABuf *buf);
    void (*free_bm)(struct VuVideoDMABuf *buf);
    int (*get_fd)(struct VuVideoDMABuf *buf);
    bool (*map_bm)(struct VuVideoDMABuf *buf);
    void (*unmap_bm)(struct VuVideoDMABuf *buf);
    void (*device_destroy)(struct vuvbm_device *dev);

    GHashTable *resource_uuids;
};

typedef struct VuVideo {
    VugDev dev;
    struct virtio_video_config virtio_config;
    GMainLoop *loop;
    struct v4l2_device *v4l2_dev;
    GList *streams;
    struct vuvbm_device *bm_dev;
} VuVideo;

struct v4l2_device {
    const gchar *devname;
    unsigned int dev_type;
    unsigned int capabilities;
    int fd;
    int epollfd;
    int opened;
    bool has_mplane;
    bool sup_dyn_res_switching;
};

struct vu_video_ctrl_command {
    VuVirtqElement elem;
    VuVirtq *vq;
    VuDev *dev;
    struct virtio_video_cmd_hdr *cmd_hdr;
    uint32_t error;
    bool finished;
    uint8_t *cmd_buf;
};


typedef struct VuVideoDMABuf {
    struct vuvbm_device *dev;
    int memfd;
    int dmafd;

    void *start;
    size_t length;
} VuVideoDMABuf;

/*
 * Structure to track internal state of a Stream
 */
struct stream {
    struct virtio_video_stream_create vio_stream;
    uint32_t stream_id;
    GList *inputq_resources;
    GList *outputq_resources;
    VuVideo *video;
    GThread *worker_thread;
    uint32_t stream_state;
    GMutex mutex;
    GCond stream_cond;
    bool output_streaming;
    bool capture_streaming;
    bool subscribed_events;
    bool has_mplane;
    int fd;
    uint32_t output_bufcount;
    uint32_t capture_bufcount;
};

#define STREAM_STOPPED      1
#define STREAM_STREAMING    2
#define STREAM_DRAINING     3
#define STREAM_DESTROYING   4
#define STREAM_DESTROYED    5

/* Structure to track resources */

struct resource {
    uint32_t stream_id;
    QemuUUID uuid;
    struct virtio_video_resource_create vio_resource;
    struct virtio_video_resource_queue vio_res_q;
    struct iovec *iov;
    uint32_t iov_count;
    uint32_t v4l2_index;
    struct VuVideoDMABuf *buf;
    enum v4l2_buf_type type;
    struct vu_video_ctrl_command *vio_q_cmd;
    bool queued;
};

struct video_format_frame_rates {
    struct virtio_video_format_range frame_rates;
    struct v4l2_frmivalenum v4l_ival;
};

struct video_format_frame {
    struct virtio_video_format_frame frame;
    struct v4l2_frmsizeenum v4l_framesize;
    GList *frm_rate_l;
};

struct video_format {
    struct v4l2_fmtdesc fmt;
    struct virtio_video_format_desc desc;
    GList *vid_fmt_frm_l;
};

/* function prototypes */
int v4l2_stream_create(struct v4l2_device *dev,
                       uint32_t vio_codedformat, struct stream *s);
void v4l2_to_virtio_video_params(struct v4l2_device *dev,
                                 struct v4l2_format *fmt,
                                 struct v4l2_selection *sel,
                                 struct virtio_video_get_params_resp *resp);

void v4l2_to_virtio_fmtdesc(struct v4l2_device *dev,
                            struct video_format *vid_fmt,
                            enum v4l2_buf_type type);

void v4l2_to_virtio_event(struct v4l2_event *ev,
                          struct virtio_video_event *vio_ev);

struct resource *find_resource_by_v4l2index(struct stream *s,
                                             enum v4l2_buf_type buf_type,
                                             uint32_t v4l2_index);
/*
 * The following conversion helpers and tables taken from Linux
 * frontend driver from opensynergy
 */

uint32_t virtio_video_level_to_v4l2(uint32_t level);
uint32_t virtio_video_v4l2_level_to_virtio(uint32_t v4l2_level);
uint32_t virtio_video_profile_to_v4l2(uint32_t profile);
uint32_t virtio_video_v4l2_profile_to_virtio(uint32_t v4l2_profile);
uint32_t virtio_video_format_to_v4l2(uint32_t format);
uint32_t virtio_video_v4l2_format_to_virtio(uint32_t v4l2_format);
uint32_t virtio_video_control_to_v4l2(uint32_t control);
uint32_t virtio_video_v4l2_control_to_virtio(uint32_t v4l2_control);
__le64 virtio_fmtdesc_generate_mask(GList **p_list);

/* Helpers for logging */
const char *vio_queue_name(enum virtio_video_queue_type queue);

static inline void
virtio_video_ctrl_hdr_letoh(struct virtio_video_cmd_hdr *hdr)
{
    hdr->type = le32toh(hdr->type);
    hdr->stream_id = le32toh(hdr->stream_id);
}

static inline void
virtio_video_ctrl_hdr_htole(struct virtio_video_cmd_hdr *hdr)
{
    hdr->type = htole32(hdr->type);
    hdr->stream_id = htole32(hdr->stream_id);
}
#endif
