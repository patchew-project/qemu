/*
 * Virtio vhost-user VIDEO Device
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

#ifndef V4L2_BACKEND_H
#define V4L2_BACKEND_H

#include "standard-headers/linux/virtio_video.h"
#include "virtio_video_helpers.h"

#define MAX_CAPS_LEN 4096
#define MAX_FMT_DESCS 64

#define STATEFUL_ENCODER (1 << 0)
#define STATEFUL_DECODER (1 << 1)
#define STATELESS_ENCODER (1 << 2)
#define STATELESS_DECODER (1 << 3)

#define V4L2_TYPE_IS_SINGLEPLANAR(type)         \
    ((type) == V4L2_BUF_TYPE_VIDEO_CAPTURE      \
     || (type) == V4L2_BUF_TYPE_VIDEO_OUTPUT)

/* Function protoypes */
GByteArray *create_query_cap_resp(struct virtio_video_query_capability *qcmd,
                                  GList **fmt_l, GByteArray *querycapresp);

/* video to v4l2 interactions */
int video_resource_create(struct stream *s,
                          uint32_t queue_type, uint32_t queue_len);
int video_send_decoder_start_cmd(struct v4l2_device *dev);
void video_free_frame_intervals(GList *frm_intervals_l);
void video_free_frame_sizes(GList *frm_sz_l);
int video_enum_formats(struct v4l2_device *dev, enum v4l2_buf_type type,
                       GList **p_fmt_list, bool only_enum_fmt);
void video_free_formats(GList **fmt_l);
int video_streamon(struct stream *s, struct v4l2_device *dev,
                   enum v4l2_buf_type type);
int video_streamoff(struct stream *s, enum v4l2_buf_type type);
int video_free_buffers(int fd, enum v4l2_buf_type type,
                       enum v4l2_memory memory);

struct v4l2_device *v4l2_backend_init(const gchar *devname);
void v4l2_backend_free(struct v4l2_device *dev);

/* v4l2 wrappers */
enum v4l2_buf_type
get_v4l2_buf_type(enum virtio_video_queue_type queue_type, bool has_mplane);
enum v4l2_memory get_v4l2_memory(enum virtio_video_mem_type mem_type);
enum virtio_video_mem_type
get_queue_mem_type(struct stream *s,
                   enum virtio_video_queue_type queue_type);

void v4l2_set_device_type(struct v4l2_device *dev, enum v4l2_buf_type type,
                          struct v4l2_fmtdesc *fmt_desc);
int v4l2_video_get_format(int fd, enum v4l2_buf_type type,
                          struct v4l2_format *fmt);
int v4l2_video_set_format(int fd, enum v4l2_buf_type type,
                          struct virtio_video_params *p);
int v4l2_set_pixel_format(int fd, enum v4l2_buf_type buf_type,
                          uint32_t pixelformat);

int v4l2_dmabuf_lookup_mplane(struct vuvbm_device *dev,
                              struct resource *res,
                              unsigned int iov_cnt);
int v4l2_dmabuf_lookup(struct vuvbm_device *dev,
                       struct resource *res,
                       unsigned int iov_len);
int v4l2_queue_buffer(enum v4l2_buf_type type,
                      enum v4l2_memory memory,
                      struct virtio_video_resource_queue *qcmd,
                      struct resource *res, struct stream *s,
                      struct v4l2_device *dev,
                      struct vuvbm_device *bm_dev);
int v4l2_dequeue_buffer(int fd, enum v4l2_buf_type type,
                        enum v4l2_memory memory,
                        struct stream *s);

int v4l2_video_get_param(int fd, enum v4l2_buf_type type,
                         struct v4l2_streamparm *param);

int v4l2_video_get_selection(int fd, enum v4l2_buf_type type,
                             struct v4l2_selection *sel);
int v4l2_video_set_selection(int fd, enum v4l2_buf_type type,
                             struct v4l2_selection *sel);

int v4l2_streamon(struct stream *s, enum v4l2_buf_type type);
int v4l2_streamoff(struct stream *s, enum v4l2_buf_type type);

int v4l2_open(const gchar *devname);
int v4l2_close(int fd);

/* ioctl wrappers */
int v4l2_ioctl_query_control(int fd, uint32_t control, int32_t *value);
int v4l2_ioctl_get_control(int fd, uint32_t control, int32_t *value);
int v4l2_ioctl_reqbuf(int fd, enum v4l2_buf_type type,
                      enum v4l2_memory memory, int *count);
int v4l2_ioctl_subscribe_event(int fd, uint32_t event_type, uint32_t id);

int v4l2_issue_cmd(int fd,  uint32_t cmd, uint32_t flags);

#endif
