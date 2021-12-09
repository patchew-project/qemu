/*
 * Virtio vhost-user VIDEO Device
 *
 * Copyright Linaro 2021
 *
 * Authors: *     Peter Griffin <peter.griffin@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef V4L2_BACKEND_H
#define V4L2_BACKEND_H

#include "standard-headers/linux/virtio_video.h"
#include "virtio_video_helpers.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAX_CAPS_LEN 4096
#define MAX_FMT_DESCS 64

#define STATEFUL_ENCODER (1 << 0)
#define STATEFUL_DECODER (1 << 1)
#define STATELESS_ENCODER (1 << 2)
#define STATELESS_DECODER (1 << 3)

/* Function protoypes */

struct v4l2_device *v4l2_backend_init(const gchar *devname);
void v4l2_backend_free(struct v4l2_device *dev);

GByteArray *create_query_cap_resp(struct virtio_video_query_capability *qcmd,
                            GList **fmt_l, GByteArray *querycapresp);
enum v4l2_buf_type get_v4l2_buf_type (enum virtio_video_queue_type queue_type,
                                      bool has_mplane);

void v4l2_set_device_type(struct v4l2_device *dev, enum v4l2_buf_type type,
                          struct v4l2_fmtdesc *fmt_desc);
int v4l2_video_get_format(int fd, enum v4l2_buf_type type,
                          struct v4l2_format *fmt);

int v4l2_video_set_format(int fd, enum v4l2_buf_type type,
                          struct virtio_video_params *p);

int v4l2_video_get_control(int fd, uint32_t control, int32_t *value);

int v4l2_queue_buffer(int fd, enum v4l2_buf_type type,
                      struct virtio_video_resource_queue *qcmd,
                      struct resource *res, struct stream *s,
                      struct v4l2_device *dev);

int v4l2_dequeue_buffer(int fd, enum v4l2_buf_type type,
                        struct stream *s);

int v4l2_dequeue_event(struct v4l2_device *dev);

int v4l2_set_pixel_format(int fd, enum v4l2_buf_type buf_type,
                          uint32_t pixelformat);
int v4l2_release_buffers(int fd, enum v4l2_buf_type type);
int v4l2_resource_create(struct stream *s, enum v4l2_buf_type type,
                         enum virtio_video_mem_type mem_type,
                         struct resource *res);
int v4l2_subscribe_event(struct stream *s,
                         uint32_t event_type, uint32_t id);

int v4l2_video_get_param(int fd, enum v4l2_buf_type type,
                         struct v4l2_streamparm *param);

int v4l2_video_get_selection(int fd, enum v4l2_buf_type type,
                             struct v4l2_selection *sel);

int v4l2_video_set_selection(int fd, enum v4l2_buf_type type,
                             struct v4l2_selection *sel);

int video_send_decoder_start_cmd(struct v4l2_device *dev);
void video_free_frame_intervals(GList *frm_intervals_l);
void video_free_frame_sizes(GList *frm_sz_l);
int video_enum_formats(struct v4l2_device *dev, enum v4l2_buf_type type,
                       GList **p_fmt_list, bool only_enum_fmt);
void video_free_formats(GList **fmt_l);
bool video_is_mplane(enum v4l2_buf_type type);
bool video_is_splane(enum v4l2_buf_type type);
bool video_is_meta(enum v4l2_buf_type type);
bool is_capture_queue(enum v4l2_buf_type type);
bool is_output_queue(enum v4l2_buf_type type);
int ioctl_streamon(struct stream *s, enum v4l2_buf_type type);
int ioctl_streamoff(struct stream *s, enum v4l2_buf_type type);
int v4l2_streamon(struct v4l2_device *dev, enum v4l2_buf_type type,
                  struct stream *s);
int v4l2_streamoff(enum v4l2_buf_type type, struct stream *s);
void v4l2_print_event(const struct v4l2_event *ev);
int v4l2_open(const gchar *devname);
int v4l2_close(int fd);
int v4l2_free_buffers(int fd, enum v4l2_buf_type type);
void convert_to_timeval(uint64_t timestamp, struct timeval *t);
int v4l2_issue_cmd(int fd,  uint32_t cmd, uint32_t flags);

#endif
