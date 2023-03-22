// SPDX-License-Identifier: GPL-2.0+
/*
 * vhost-user-video header
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

#ifndef VUVIDEO_H
#define VUVIDEO_H

#include "virtio_video_helpers.h"
#include "v4l2_backend.h"

GList *get_resource_list(struct stream *s, uint32_t queue_type);
void send_qclear_res_reply(gpointer data, gpointer user_data);

struct stream *find_stream(struct VuVideo *v, uint32_t stream_id);
int add_resource(struct stream *s, struct resource *r, uint32_t queue_type);
int remove_resource(struct stream *s, struct resource *r, uint32_t queue_type);
struct resource *find_resource(struct stream *s, uint32_t resource_id,
                                uint32_t queue_type);

void send_ctrl_response(struct vu_video_ctrl_command *vio_cmd,
                       uint8_t *resp, size_t resp_len);

void send_ctrl_response_nodata(struct vu_video_ctrl_command *vio_cmd);

void free_resource_mem(struct resource *r);
void remove_all_resources(struct stream *s, uint32_t queue_type);

void handle_queue_clear_cmd(struct VuVideo *v,
                           struct vu_video_ctrl_command *vio_cmd);

/* virtio_video_udmabuf.c */
bool vuvbm_buffer_create(struct vuvbm_device *dev,
                         struct VuVideoDMABuf *buffer,
                         uint32_t len);
void vuvbm_init_device(struct vuvbm_device *dev);
struct VuVideoDMABuf *vuvbm_lookup(struct vuvbm_device *dev, QemuUUID uuid);
void vuvbm_buffer_destroy(struct VuVideoDMABuf *buffer);
void vuvbm_device_destroy(struct vuvbm_device *dev);

#endif
