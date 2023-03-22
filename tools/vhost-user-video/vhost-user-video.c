/*
 * VIRTIO Video Emulation via vhost-user
 *
 * Copyright (c) 2023 Red Hat, Inc.
 * Copyright (c) 2021 Linaro Ltd
 *
 * Authors:
 *      Peter Griffin <peter.griffin@linaro.org>
 *      Albert Esteve <aesteve@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "vhost-user-video"
#define G_LOG_USE_STRUCTURED 1

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <endian.h>
#include <assert.h>

#include "libvhost-user-glib.h"
#include "libvhost-user.h"
#include "standard-headers/linux/virtio_video.h"

#include "qemu/compiler.h"
#include "qemu/iov.h"

#include "vuvideo.h"
#include "v4l2_backend.h"
#include "virtio_video_helpers.h"

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) * __mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member)); })
#endif

static gchar *socket_path;
static gchar *v4l2_path;
static gint socket_fd = -1;
static gboolean print_cap;
static gboolean verbose;
static gboolean debug;

static GOptionEntry options[] = {
    { "socket-path", 0, 0, G_OPTION_ARG_FILENAME, &socket_path,
      "Location of vhost-user Unix domain socket, "
      "incompatible with --fd", "PATH" },
    { "v4l2-device", 0, 0, G_OPTION_ARG_FILENAME, &v4l2_path,
      "Location of v4l2 device node", "PATH" },
    { "fd", 0, 0, G_OPTION_ARG_INT, &socket_fd,
      "Specify the fd of the backend, "
      "incompatible with --socket-path", "FD" },
    { "print-capabilities", 0, 0, G_OPTION_ARG_NONE, &print_cap,
      "Output to stdout the backend capabilities "
      "in JSON format and exit", NULL},
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
      "Be more verbose in output", NULL},
    { "debug", 0, 0, G_OPTION_ARG_NONE, &debug,
      "Include debug output", NULL},
    { NULL }
};

enum {
    VHOST_USER_VIDEO_MAX_QUEUES = 2,
};

static const char *
vv_cmd_to_string(int cmd)
{
#define CMD(cmd) [cmd] = #cmd
    static const char *vg_cmd_str[] = {
        /* Command */
        CMD(VIRTIO_VIDEO_CMD_QUERY_CAPABILITY),
        CMD(VIRTIO_VIDEO_CMD_STREAM_CREATE),
        CMD(VIRTIO_VIDEO_CMD_STREAM_DESTROY),
        CMD(VIRTIO_VIDEO_CMD_STREAM_DRAIN),
        CMD(VIRTIO_VIDEO_CMD_RESOURCE_CREATE),
        CMD(VIRTIO_VIDEO_CMD_RESOURCE_QUEUE),
        CMD(VIRTIO_VIDEO_CMD_RESOURCE_DESTROY_ALL),
        CMD(VIRTIO_VIDEO_CMD_QUEUE_CLEAR),
        CMD(VIRTIO_VIDEO_CMD_QUERY_CONTROL),
        CMD(VIRTIO_VIDEO_CMD_GET_CONTROL),
        CMD(VIRTIO_VIDEO_CMD_SET_CONTROL),
        CMD(VIRTIO_VIDEO_CMD_GET_PARAMS_EXT),
        CMD(VIRTIO_VIDEO_CMD_SET_PARAMS_EXT),
    };
#undef CMD

    if (cmd >= 0 && cmd < G_N_ELEMENTS(vg_cmd_str)) {
        return vg_cmd_str[cmd];
    } else {
        return "unknown";
    }
}

static void video_panic(VuDev *dev, const char *msg)
{
    g_critical("%s\n", msg);
    exit(EXIT_FAILURE);
}

static uint64_t video_get_features(VuDev *dev)
{
    g_info("%s: replying", __func__);
    return 0;
}

static void video_set_features(VuDev *dev, uint64_t features)
{
    if (features) {
        g_autoptr(GString) s = g_string_new("Requested un-handled feature");
        g_string_append_printf(s, " 0x%" PRIx64 "", features);
        g_info("%s: %s", __func__, s->str);
    }
}

/*
 * The configuration of the device is static and set when we start the
 * daemon.
 */
static int
video_get_config(VuDev *dev, uint8_t *config, uint32_t len)
{
    VuVideo *v = container_of(dev, VuVideo, dev.parent);

    g_return_val_if_fail(len <= sizeof(struct virtio_video_config), -1);
    v->virtio_config.version = 0;
    v->virtio_config.max_caps_length = MAX_CAPS_LEN;
    v->virtio_config.max_resp_length = MAX_CAPS_LEN;

    memcpy(config, &v->virtio_config, len);

    g_debug("%s: config.max_caps_length = %d", __func__
           , ((struct virtio_video_config *)config)->max_caps_length);
    g_debug("%s: config.max_resp_length = %d", __func__
           , ((struct virtio_video_config *)config)->max_resp_length);

    return 0;
}

static int
video_set_config(VuDev *dev, const uint8_t *data,
                 uint32_t offset, uint32_t size,
                 uint32_t flags)
{
    g_debug("%s: ", __func__);
    /*
     * set_config is required to set the F_CONFIG feature,
     * but we can just ignore the call
     */
    return 0;
}

/*
 * Handlers for individual control messages
 */

static void
handle_set_params_cmd(struct VuVideo *v, struct vu_video_ctrl_command *vio_cmd)
{
    int ret = 0;
    enum v4l2_buf_type buf_type;
    struct virtio_video_set_params *cmd =
        (struct virtio_video_set_params *) vio_cmd->cmd_buf;
    struct stream *s;

    g_debug("%s: type(0x%x) resource_type(%d) stream_id(%d) %s ",
            __func__, cmd->hdr.type,
            le32toh(cmd->params.resource_type), cmd->hdr.stream_id,
            vio_queue_name(le32toh(cmd->params.queue_type)));
    g_debug("%s: format=0x%x frame_width(%d) frame_height(%d)",
            __func__, le32toh(cmd->params.format),
            le32toh(cmd->params.frame_width),
            le32toh(cmd->params.frame_height));
    g_debug("%s: min_buffers(%d) max_buffers(%d)", __func__,
            le32toh(cmd->params.min_buffers), le32toh(cmd->params.max_buffers));
    g_debug("%s: frame_rate(%d) num_planes(%d)", __func__,
            le32toh(cmd->params.frame_rate), le32toh(cmd->params.num_planes));
    g_debug("%s: crop top=%d, left=%d, width=%d, height=%d", __func__,
            le32toh(cmd->params.crop.left), le32toh(cmd->params.crop.top),
            le32toh(cmd->params.crop.width), le32toh(cmd->params.crop.height));

    s = find_stream(v, cmd->hdr.stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found", __func__, cmd->hdr.stream_id);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        return;
    }

    g_mutex_lock(&s->mutex);

    buf_type = get_v4l2_buf_type(le32toh(cmd->params.queue_type),
                                 s->has_mplane);

    ret = v4l2_video_set_format(s->fd, buf_type, &cmd->params);
    if (ret < 0) {
        g_error("%s: v4l2_video_set_format() failed", __func__);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out_unlock;
    }

    if (V4L2_TYPE_IS_CAPTURE(buf_type)) {
        /* decoder supports composing on CAPTURE */
        struct v4l2_selection sel;
        memset(&sel, 0, sizeof(struct v4l2_selection));

        sel.r.left = le32toh(cmd->params.crop.left);
        sel.r.top = le32toh(cmd->params.crop.top);
        sel.r.width = le32toh(cmd->params.crop.width);
        sel.r.height = le32toh(cmd->params.crop.height);

        ret = v4l2_video_set_selection(s->fd, buf_type, &sel);
        if (ret < 0) {
            g_printerr("%s: v4l2_video_set_selection failed: %s (%d).\n"
                       , __func__, g_strerror(errno), errno);
            cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
            goto out_unlock;
        }
    }

    cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;

out_unlock:
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
    return;
}

static void
handle_get_params_cmd(struct VuVideo *v, struct vu_video_ctrl_command *vio_cmd)
{
    int ret;
    struct v4l2_format fmt;
    struct v4l2_selection sel;
    enum v4l2_buf_type buf_type;
    struct virtio_video_get_params *cmd =
        (struct virtio_video_get_params *) vio_cmd->cmd_buf;
    struct virtio_video_get_params_resp getparams_reply;
    struct stream *s;

    g_debug("%s: type(0x%x) stream_id(%d) %s", __func__,
            cmd->hdr.type, cmd->hdr.stream_id,
            vio_queue_name(le32toh(cmd->queue_type)));

    s = find_stream(v, cmd->hdr.stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found\n"
                   , __func__, cmd->hdr.stream_id);
        getparams_reply.hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        return;
    }

    g_mutex_lock(&s->mutex);

    getparams_reply.hdr.stream_id = cmd->hdr.stream_id;
    getparams_reply.params.queue_type = cmd->queue_type;

    buf_type = get_v4l2_buf_type(cmd->queue_type, s->has_mplane);

    ret = v4l2_video_get_format(s->fd, buf_type, &fmt);
    if (ret < 0) {
        g_printerr("v4l2_video_get_format failed\n");
        getparams_reply.hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out_unlock;
    }

    if (V4L2_TYPE_IS_CAPTURE(buf_type)) {
        ret = v4l2_video_get_selection(s->fd, buf_type, &sel);
        if (ret < 0) {
            g_printerr("v4l2_video_get_selection failed\n");
            getparams_reply.hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
            goto out_unlock;
        }
    }

    /* convert from v4l2 to virtio */
    v4l2_to_virtio_video_params(v->v4l2_dev, &fmt, &sel,
                                &getparams_reply);

    getparams_reply.hdr.type = VIRTIO_VIDEO_RESP_OK_GET_PARAMS;

out_unlock:
    vio_cmd->finished = true;
    send_ctrl_response(vio_cmd, (uint8_t *)&getparams_reply,
                       sizeof(struct virtio_video_get_params_resp));
    g_mutex_unlock(&s->mutex);
}

struct stream *find_stream(struct VuVideo *v, uint32_t stream_id)
{
    GList *l;
    struct stream *s;

    for (l = v->streams; l != NULL; l = l->next) {
        s = (struct stream *)l->data;
        if (s->stream_id == stream_id) {
            return s;
        }
    }

    return NULL;
}

int add_resource(struct stream *s, struct resource *r, uint32_t queue_type)
{

    if (!s || !r) {
        return -EINVAL;
    }

    switch (queue_type) {
    case VIRTIO_VIDEO_QUEUE_TYPE_INPUT:
        s->inputq_resources = g_list_append(s->inputq_resources, r);
        break;

    case VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT:
        s->outputq_resources = g_list_append(s->outputq_resources, r);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

void free_resource_mem(struct resource *r)
{

    /*
     * Frees the memory allocated for resource_queue_cmd
     * not the memory allocated in resource_create
     */

    if (r->vio_q_cmd) {
        g_free(r->vio_q_cmd->cmd_buf);
        r->vio_q_cmd->cmd_buf = NULL;
        free(r->vio_q_cmd);
        r->vio_q_cmd = NULL;
    }
}

void remove_all_resources(struct stream *s, uint32_t queue_type)
{
    GList **resource_list;
    struct resource *r;

    /* assumes stream mutex is held by caller */

    switch (queue_type) {
    case VIRTIO_VIDEO_QUEUE_TYPE_INPUT:
        resource_list = &s->inputq_resources;
        break;
    case VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT:
        resource_list = &s->outputq_resources;
        break;
    default:
        g_critical("%s: Invalid virtio queue!", __func__);
        return;
    }

    g_debug("%s: resource_list has %d elements", __func__
            , g_list_length(*resource_list));

    GList *l = *resource_list;
    while (l != NULL) {
        GList *next = l->next;
        r = (struct resource *)l->data;
        if (r) {
            g_debug("%s: Removing resource_id(%d) resource=%p"
                    , __func__, r->vio_resource.resource_id, r);

            /*
             * Assumes that either QUEUE_CLEAR or normal dequeuing
             * of buffers will have freed resource_queue cmd memory
             */

            /* free resource memory allocated in resource_create() */
            g_free(r->iov);
            if (r->buf != NULL) {
                vuvbm_buffer_destroy(r->buf);
            }
            g_free(r);
            *resource_list = g_list_delete_link(*resource_list, l);
        }
        l = next;
   }
}

struct resource *find_resource(struct stream *s, uint32_t resource_id,
                                uint32_t queue_type)
{
    GList *l;
    struct resource *r;

    switch (queue_type) {
    case VIRTIO_VIDEO_QUEUE_TYPE_INPUT:
        l = s->inputq_resources;
        break;

    case VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT:
        l = s->outputq_resources;
        break;
    default:
        g_error("%s: Invalid queue type!", __func__);
        return NULL;
    }

    for (; l != NULL; l = l->next) {
        r = (struct resource *)l->data;
        if (r->vio_resource.resource_id == resource_id) {
            return r;
        }
    }

    return NULL;
}

struct resource *find_resource_by_v4l2index(struct stream *s,
                                             enum v4l2_buf_type buf_type,
                                             uint32_t v4l2_index)
{
    GList *l;
    struct resource *r;

    switch (buf_type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
        l = s->outputq_resources;
        break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
        l = s->inputq_resources;
        break;

    default:
        g_error("Unsupported buffer type\n");
        return NULL;
    }

    for (; l != NULL; l = l->next) {
        r = (struct resource *)l->data;
        if (r->v4l2_index == v4l2_index) {
            g_debug("%s: found Resource=%p streamid(%d) resourceid(%d) "
                    "numplanes(%d) planes_layout(0x%x) vio_q_cmd=%p", __func__,
                    r, r->stream_id, r->vio_resource.resource_id,
                    r->vio_resource.num_planes, r->vio_resource.planes_layout,
                    r->vio_q_cmd);
            return r;
        }
    }
    return NULL;
}

#define EVENT_WQ_IDX 1

static void *stream_worker_thread(gpointer data)
{
    int ret;
    struct stream *s = data;
    VuVideo *v = s->video;
    VugDev *vugdev = &v->dev;
    VuDev *vudev = &vugdev->parent;
    VuVirtq *vq = vu_get_queue(vudev, EVENT_WQ_IDX);
    VuVirtqElement *elem;
    size_t len;

    struct v4l2_event ev;
    struct virtio_video_event vio_event;

    /* select vars */
    fd_set efds, rfds, wfds;
    bool have_event, have_read, have_write;
    enum v4l2_buf_type buf_type;

    fcntl(s->fd, F_SETFL, fcntl(s->fd, F_GETFL) | O_NONBLOCK);

    while (true) {
        int res;

        g_mutex_lock(&s->mutex);

        g_debug("Stream: id %d state %d", s->stream_id, s->stream_state);
        /* wait for STREAMING or DESTROYING state */
        while (s->stream_state != STREAM_DESTROYING &&
               s->stream_state != STREAM_STREAMING &&
               s->stream_state != STREAM_DRAINING)
            g_cond_wait(&s->stream_cond, &s->mutex);

        if (s->stream_state == STREAM_DESTROYING) {
            g_debug("stream worker thread exiting!");
            s->stream_state = STREAM_DESTROYED;
            g_cond_signal(&s->stream_cond);
            g_mutex_unlock(&s->mutex);
            g_thread_exit(0);
        }

        g_mutex_unlock(&s->mutex);

        FD_ZERO(&efds);
        FD_SET(s->fd, &efds);
        FD_ZERO(&rfds);
        FD_SET(s->fd, &rfds);
        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);

        struct timeval tv = { 0 , 500000 };
        res = select(s->fd + 1, &rfds, &wfds, &efds, &tv);
        if (res < 0) {
            g_printerr("%s:%d - select() failed: %s (%d)\n",
                       __func__, __LINE__, g_strerror(errno), errno);
            break;
        }

        if (res == 0) {
            g_debug("%s:%d - select() timeout", __func__, __LINE__);
            continue;
        }

        have_event = FD_ISSET(s->fd, &efds);
        have_read = FD_ISSET(s->fd, &rfds);
        have_write = FD_ISSET(s->fd, &wfds);
        /* read is capture queue, write is output queue */

        g_debug("%s:%d have_event=%d, have_write=%d, have_read=%d\n",
                __func__, __LINE__, FD_ISSET(s->fd, &efds),
                FD_ISSET(s->fd, &wfds), FD_ISSET(s->fd, &rfds));

        g_mutex_lock(&s->mutex);

        if (have_event) {
            g_debug("%s: have_event!", __func__);
            res = ioctl(s->fd, VIDIOC_DQEVENT, &ev);
            if (res < 0) {
                g_printerr("%s:%d - VIDIOC_DQEVENT failed: %s (%d)\n",
                           __func__, __LINE__, g_strerror(errno), errno);
                break;
            }
            v4l2_to_virtio_event(&ev, &vio_event);

            /* get event workqueue */
            elem = vu_queue_pop(vudev, vq, sizeof(struct VuVirtqElement));
            if (!elem) {
                g_debug("%s:%d\n", __func__, __LINE__);
                break;
            }

            len = iov_from_buf_full(elem->in_sg,
                                    elem->in_num, 0, (void *) &vio_event,
                                    sizeof(struct virtio_video_event));

            if (vio_event.event_type) {
                vu_queue_push(vudev, vq, elem, len);
                vu_queue_notify(vudev, vq);
            }
        }

        if (have_read && s->capture_streaming) {
            /* TODO assumes decoder */
            buf_type = s->has_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                : V4L2_BUF_TYPE_VIDEO_CAPTURE;

            enum virtio_video_mem_type mem_type =
                get_queue_mem_type(s, VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
            enum v4l2_memory memory = get_v4l2_memory(mem_type);

            ret = v4l2_dequeue_buffer(s->fd, buf_type, memory, s);
            if (ret < 0) {
                g_info("%s: v4l2_dequeue_buffer() failed CAPTURE ret(%d)",
                       __func__, ret);

                if (ret == -EPIPE) {
                    g_debug("Dequeued last buffer, stop streaming.");
                    /* dequeued last buf, so stop streaming */
                    v4l2_streamoff(s, buf_type);
                }
            }
        }

        if (have_write && s->output_streaming) {
            buf_type = s->has_mplane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
                : V4L2_BUF_TYPE_VIDEO_OUTPUT;

            enum virtio_video_mem_type mem_type =
                get_queue_mem_type(s, VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
            enum v4l2_memory memory = get_v4l2_memory(mem_type);

            ret = v4l2_dequeue_buffer(s->fd, buf_type, memory, s);
            if (ret < 0) {
                g_info("%s: v4l2_dequeue_buffer() failed OUTPUT ret(%d)",
                       __func__, ret);
            }
        }

        g_mutex_unlock(&s->mutex);
    }

    return NULL;
}

void handle_queue_clear_cmd(struct VuVideo *v,
                       struct vu_video_ctrl_command *vio_cmd)
{
    struct virtio_video_queue_clear *cmd =
        (struct virtio_video_queue_clear *)vio_cmd->cmd_buf;
    int ret = 0;
    struct stream *s;
    uint32_t stream_id = le32toh(cmd->hdr.stream_id);
    enum virtio_video_queue_type queue_type = le32toh(cmd->queue_type);
    GList *res_list = NULL;

    g_debug("%s: stream_id(%d) %s\n", __func__, stream_id,
            vio_queue_name(queue_type));

    if (!v || !cmd) {
        return;
    }

    s = find_stream(v, stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found", __func__, stream_id);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        return;
    }

    g_mutex_lock(&s->mutex);

    enum v4l2_buf_type buf_type =
        get_v4l2_buf_type(le32toh(cmd->queue_type), s->has_mplane);

    /*
     * QUEUE_CLEAR behaviour from virtio-video spec
     * Return already queued buffers back from the input or the output queue
     * of the device. The device SHOULD return all of the buffers from the
     * respective queue as soon as possible without pushing the buffers through
     * the processing pipeline.
     *
     * From v4l2 PoV we issue a VIDIOC_STREAMOFF on the queue which will abort
     * or finish any DMA in progress, unlocks any user pointer buffers locked
     * in physical memory, and it removes all buffers from the incoming and
     * outgoing queues.
     */

    /* issue streamoff  */
    ret = v4l2_streamoff(s, buf_type);
    if (ret < 0) {
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out_unlock;
    }

    /* iterate the queues resources list - and send a reply to each one */

    /*
     * If the processing was stopped due to VIRTIO_VIDEO_CMD_QUEUE_CLEAR,
     * the device MUST respond with VIRTIO_VIDEO_RESP_OK_NODATA as a response
     * type and VIRTIO_- VIDEO_BUFFER_FLAG_ERR in flags.
     */

    res_list = get_resource_list(s, queue_type);
    g_list_foreach(res_list, (GFunc)send_qclear_res_reply, s);
    cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;

out_unlock:
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
}

GList *get_resource_list(struct stream *s, uint32_t queue_type)
{
    switch (queue_type) {
    case VIRTIO_VIDEO_QUEUE_TYPE_INPUT:
        return s->inputq_resources;
        break;

    case VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT:
        return s->outputq_resources;
        break;
    default:
        g_critical("%s: Unknown queue type!", __func__);
        return NULL;
    }
}

void send_ctrl_response(struct vu_video_ctrl_command *vio_cmd,
                       uint8_t *resp, size_t resp_len)
{
    size_t len;

    virtio_video_ctrl_hdr_htole((struct virtio_video_cmd_hdr *)resp);

    /* send virtio_video_resource_queue_resp */
    len = iov_from_buf_full(vio_cmd->elem.in_sg,
                            vio_cmd->elem.in_num, 0, resp, resp_len);

    if (len != resp_len) {
        g_critical("%s: response size incorrect %zu vs %zu",
                   __func__, len, resp_len);
    }

    vu_queue_push(vio_cmd->dev, vio_cmd->vq, &vio_cmd->elem, len);
    vu_queue_notify(vio_cmd->dev, vio_cmd->vq);

    if (vio_cmd->finished) {
        g_free(vio_cmd->cmd_buf);
        free(vio_cmd);
    }
}

void send_ctrl_response_nodata(struct vu_video_ctrl_command *vio_cmd)
{
    send_ctrl_response(vio_cmd, vio_cmd->cmd_buf,
                       sizeof(struct virtio_video_cmd_hdr));
}

void send_qclear_res_reply(gpointer data, gpointer user_data)
{
    struct resource *r = data;
    if (!r->queued) {
        /*
         * only need to send replies for buffers that are
         * inflight
         */
        return;
    }

    struct vu_video_ctrl_command *vio_cmd = r->vio_q_cmd;
    struct virtio_video_queue_clear *cmd =
        (struct virtio_video_queue_clear *) vio_cmd->cmd_buf;
    struct virtio_video_resource_queue_resp resp;



    resp.hdr.stream_id = cmd->hdr.stream_id;
    resp.hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;
    resp.flags = htole32(VIRTIO_VIDEO_BUFFER_FLAG_ERR);
    resp.timestamp = htole64(r->vio_res_q.timestamp);

    g_debug("%s: stream_id=%d type=0x%x flags=0x%x resource_id=%d t=%llx"
            , __func__, resp.hdr.stream_id, resp.hdr.type, resp.flags,
            r->vio_resource.resource_id, resp.timestamp);

    vio_cmd->finished = true;
    send_ctrl_response(vio_cmd, (uint8_t *) &resp,
                        sizeof(struct virtio_video_resource_queue_resp));
}

static int
handle_resource_create_cmd(struct VuVideo *v,
                           struct vu_video_ctrl_command *vio_cmd)
{
    int ret = 0, i;
    uint32_t total_entries = 0;
    uint32_t stream_id;
    struct virtio_video_resource_create *cmd =
        (struct virtio_video_resource_create *)vio_cmd->cmd_buf;
    struct resource *res;
    struct virtio_video_resource_create *r;
    struct stream *s;
    enum virtio_video_mem_type mem_type;

    stream_id = cmd->hdr.stream_id;

    s = find_stream(v, stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found", __func__, stream_id);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        return ret;
    }

    g_mutex_lock(&s->mutex);

    if (le32toh(cmd->resource_id) == 0) {
        g_critical("%s: resource id 0 is not allowed", __func__);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out_unlock;
    }

    /* check resource id doesn't already exist */
    res = find_resource(s, le32toh(cmd->resource_id), le32toh(cmd->queue_type));
    if (res) {
        g_critical("%s: resource_id:%d already exists"
                   , __func__, le32toh(cmd->resource_id));
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_RESOURCE_ID;
        goto out_unlock;
    } else {
        res = g_new0(struct resource, 1);
        res->vio_resource.resource_id = le32toh(cmd->resource_id);
        res->vio_resource.queue_type = le32toh(cmd->queue_type);
        res->vio_resource.planes_layout = le32toh(cmd->planes_layout);

        res->vio_resource.num_planes = le32toh(cmd->num_planes);
        r = &res->vio_resource;

        switch (le32toh(cmd->queue_type)) {
        case VIRTIO_VIDEO_QUEUE_TYPE_INPUT:
            res->v4l2_index = g_list_length(s->inputq_resources);
            break;
        case VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT:
            res->v4l2_index = g_list_length(s->outputq_resources);
            break;
        default:
            g_critical("%s: invalid queue_type(%s) resource_id(%d)",
                       __func__, vio_queue_name(r->queue_type),
                       le32toh(cmd->resource_id));
            cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_RESOURCE_ID;
            goto out_unlock;
        }

        ret = add_resource(s, res, le32toh(cmd->queue_type));
        if (ret) {
            g_critical("%s: resource_add id:%d failed",
                       __func__, le32toh(cmd->resource_id));
            cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_RESOURCE_ID;
            goto out_unlock;
        }

        g_debug("%s: resource=%p streamid(%d) resourceid(%d) numplanes(%d) "
                "planes_layout(0x%x) %s",
                __func__, res, res->stream_id, r->resource_id, r->num_planes,
                r->planes_layout, vio_queue_name(r->queue_type));
    }

    if (r->planes_layout & VIRTIO_VIDEO_PLANES_LAYOUT_PER_PLANE) {
        g_debug("%s: streamid(%d) resourceid(%d) planes_layout(0x%x)",
                __func__, res->stream_id, r->resource_id, r->planes_layout);

        for (i = 0; i < r->num_planes; i++) {
            total_entries += le32toh(cmd->num_entries[i]);
            g_debug("%s: streamid(%d) resourceid(%d) num_entries[%d]=%d",
                    __func__, res->stream_id, r->resource_id,
                    i, le32toh(cmd->num_entries[i]));
        }
    } else {
        total_entries = 1;
    }

    /*
     * virtio_video_resource_create is followed by either
     * - struct virtio_video_mem_entry entries[]
     *   for VIRTIO_VIDEO_MEM_TYPE_GUEST_PAGES
     * - struct virtio_video_object_entry entries[]
     *   for VIRTIO_VIDEO_MEM_TYPE_VIRTIO_OBJECT
     */

    mem_type = get_queue_mem_type(s, r->queue_type);

    switch (mem_type) {
    case VIRTIO_VIDEO_MEM_TYPE_GUEST_PAGES:
    {
        struct virtio_video_mem_entry *ent;
        ent = (void *)cmd + sizeof(struct virtio_video_resource_create);

        res->iov = g_malloc0(sizeof(struct iovec) * total_entries);
        for (i = 0; i < total_entries; i++) {
            uint64_t len = le32toh(ent[i].length);
            g_debug("%s: ent[%d] addr=0x%lx",
                    __func__, i, le64toh(ent[i].addr));

            res->iov[i].iov_len = le32toh(ent[i].length);
            res->iov[i].iov_base =
                vu_gpa_to_va(&v->dev.parent, &len, le64toh(ent[i].addr));
            g_debug("%s: [%d] iov_len = 0x%lx", __func__
                    , i, res->iov[i].iov_len);
            g_debug("%s: [%d] iov_base = 0x%p", __func__
                    , i, res->iov[i].iov_base);
        }
        res->iov_count = total_entries;
    } break;
    case VIRTIO_VIDEO_MEM_TYPE_VIRTIO_OBJECT:
    {
        struct virtio_video_object_entry *ent;
        ent = (void *)cmd + sizeof(struct virtio_video_resource_create);

        memcpy(&res->uuid, ent->uuid, sizeof(ent->uuid));
        g_debug("%s: create resource uuid(%s)",
                __func__, qemu_uuid_unparse_strdup(&res->uuid));

        vuvbm_init_device(v->bm_dev);
    } break;
    }

    cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;

out_unlock:
    /* send response */
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
    return ret;
}

static int
handle_resource_queue_cmd(struct VuVideo *v,
                          struct vu_video_ctrl_command *vio_cmd)
{
    struct virtio_video_resource_queue *cmd =
        (struct virtio_video_resource_queue *)vio_cmd->cmd_buf;
    struct resource *res;
    struct stream *s;
    uint32_t stream_id;
    int ret = 0;

    g_debug("%s: type(0x%x) %s resource_id(%d)", __func__,
            cmd->hdr.type, vio_queue_name(le32toh(cmd->queue_type)),
            le32toh(cmd->resource_id));
    g_debug("%s: num_data_sizes = %d", __func__, le32toh(cmd->num_data_sizes));
    g_debug("%s: data_sizes[0] = %d", __func__, le32toh(cmd->data_sizes[0]));

    stream_id = cmd->hdr.stream_id;

    s = find_stream(v, stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found", __func__, stream_id);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        return ret;
    }

    g_mutex_lock(&s->mutex);

    if (cmd->resource_id == 0) {
        g_critical("%s: resource id 0 is not allowed", __func__);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_RESOURCE_ID;
        goto out_unlock;
    }

    if (le32toh(cmd->queue_type) == VIRTIO_VIDEO_QUEUE_TYPE_INPUT) {
        if (g_list_length(s->inputq_resources) && !s->output_bufcount) {
            ret = video_resource_create(s, le32toh(cmd->queue_type),
                                        g_list_length(s->inputq_resources));
            if (ret < 0) {
                g_critical("%s: output buffer allocation failed", __func__);
                cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
                goto out_unlock;
            }
        }
    } else {
        if (g_list_length(s->outputq_resources) && !s->capture_bufcount) {
            ret = video_resource_create(s, le32toh(cmd->queue_type),
                                        g_list_length(s->outputq_resources));
            if (ret < 0) {
                g_critical("%s: capture buffer allocation failed", __func__);
                cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
                goto out_unlock;
            }
        }
    }

    /* get resource object */
    res = find_resource(s, le32toh(cmd->resource_id), le32toh(cmd->queue_type));
    if (!res) {
        g_critical("%s: resource_id:%d does not exist!"
                   , __func__, le32toh(cmd->resource_id));
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_RESOURCE_ID;
        goto out_unlock;
    }

    res->vio_res_q.timestamp = le64toh(cmd->timestamp);
    res->vio_res_q.num_data_sizes = le32toh(cmd->num_data_sizes);
    res->vio_res_q.queue_type = le32toh(cmd->queue_type);
    res->vio_q_cmd = vio_cmd;

    g_debug("%s: res=%p res->vio_q_cmd=0x%p", __func__, res, res->vio_q_cmd);

    enum v4l2_buf_type buf_type = get_v4l2_buf_type(
        cmd->queue_type, s->has_mplane);
    enum virtio_video_mem_type mem_type =
        get_queue_mem_type(s, cmd->queue_type);
    enum v4l2_memory memory = get_v4l2_memory(mem_type);

    ret = v4l2_queue_buffer(buf_type, memory, cmd, res, s, v->v4l2_dev, v->bm_dev);
    if (ret < 0) {
        g_critical("%s: v4l2_queue_buffer failed", __func__);
        /* virtio error set by v4l2_queue_buffer */
        goto out_unlock;
    }

    /*
     * let the stream worker thread do the dequeueing of output and
     * capture queue buffers and send the resource_queue replies
     */

    g_mutex_unlock(&s->mutex);
    return ret;

out_unlock:
    /* send response */
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
    return ret;
}


static void
handle_resource_destroy_all_cmd(struct VuVideo *v,
                                struct vu_video_ctrl_command *vio_cmd)
{
    struct virtio_video_resource_destroy_all *cmd =
        (struct virtio_video_resource_destroy_all *)vio_cmd->cmd_buf;
    enum v4l2_buf_type buf_type;
    struct stream *s;
    int ret = 0;

    g_debug("%s: type(0x%x) %s stream_id(%d)", __func__,
            cmd->hdr.type, vio_queue_name(le32toh(cmd->queue_type)),
            cmd->hdr.stream_id);

    s = find_stream(v, cmd->hdr.stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found", __func__, cmd->hdr.stream_id);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out;
    }

    g_mutex_lock(&s->mutex);

    buf_type = get_v4l2_buf_type(le32toh(cmd->queue_type), s->has_mplane);
    enum virtio_video_mem_type mem_type =
        get_queue_mem_type(s, cmd->queue_type);

    ret = video_free_buffers(s->fd, buf_type, get_v4l2_memory(mem_type));
    if (ret) {
        g_critical("%s: video_free_buffers() failed", __func__);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out;
    }

    remove_all_resources(s, le32toh(cmd->queue_type));

    /* free resource objects from queue list */
    cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;

out:
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
}

static void
handle_stream_create_cmd(struct VuVideo *v,
                         struct vu_video_ctrl_command *vio_cmd)
{
    int ret = 0;
    struct stream *s;
    uint32_t req_stream_id;
    uint32_t coded_format;

    struct virtio_video_stream_create *cmd =
        (struct virtio_video_stream_create *)vio_cmd->cmd_buf;

    g_debug("%s: type(0x%x) stream_id(%d) in_mem_type(0x%x) "
            "out_mem_type(0x%x) coded_format(0x%x)",
            __func__, cmd->hdr.type, cmd->hdr.stream_id,
            le32toh(cmd->in_mem_type), le32toh(cmd->out_mem_type),
            le32toh(cmd->coded_format));

    req_stream_id = cmd->hdr.stream_id;
    coded_format = le32toh(cmd->coded_format);

    if (find_stream(v, req_stream_id)) {
        g_debug("%s: Stream ID in use - ", __func__);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_STREAM_ID;
    } else {
        s = g_new0(struct stream, 1);
        /* copy but bswap */
        s->vio_stream.in_mem_type = le32toh(cmd->in_mem_type);
        s->vio_stream.out_mem_type = le32toh(cmd->out_mem_type);
        s->vio_stream.coded_format = le32toh(cmd->coded_format);
        strncpy((char *)&s->vio_stream.tag, (char *)cmd->tag,
                sizeof(cmd->tag) - 1);
        s->vio_stream.tag[sizeof(cmd->tag) - 1] = 0;
        s->stream_id = req_stream_id;
        s->video = v;
        s->stream_state = STREAM_STOPPED;
        s->has_mplane = v->v4l2_dev->has_mplane;
        g_mutex_init(&s->mutex);
        g_cond_init(&s->stream_cond);
        v->streams = g_list_append(v->streams, s);

        cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;

        /* set the requested coded format */
        ret = v4l2_stream_create(v->v4l2_dev, coded_format, s);
        if (ret < 0) {
            g_printerr("%s: v4l2_stream_create() failed", __func__);
            cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;

            v->streams = g_list_remove(v->streams, s);
            g_free(s);
        }

        /*
         * create thread to handle
         *  - dequeing buffers from output & capture queues
         *  - sending resource replies for buffers
         *  - handling EOS and dynamic-resoltion events
         */
        s->worker_thread = g_thread_new("vio-video stream worker",
                                        stream_worker_thread, s);
    }

    /* send response */
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
}

static void
handle_stream_drain_cmd(struct VuVideo *v,
                          struct vu_video_ctrl_command *vio_cmd)
{
    int ret;
    struct stream *s;
    uint32_t stream_id;
    struct virtio_video_stream_drain *cmd =
        (struct virtio_video_stream_drain *)vio_cmd->cmd_buf;

    stream_id = cmd->hdr.stream_id;

    g_debug("%s: stream_id(%d)", __func__, stream_id);

    s = find_stream(v, stream_id);
    if (!s) {
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_STREAM_ID;
        return;
    }

    g_debug("%s: Found stream=0x%p", __func__, s);

    g_mutex_lock(&s->mutex);

    ret = v4l2_issue_cmd(s->fd, V4L2_DEC_CMD_STOP, 0);
    if (ret < 0) {
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        goto out_unlock;
    }
    s->stream_state = STREAM_DRAINING;
    g_cond_signal(&s->stream_cond);

    cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;

out_unlock:
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
}

static void
handle_stream_destroy_cmd(struct VuVideo *v,
                          struct vu_video_ctrl_command *vio_cmd)
{
    struct stream *s;
    uint32_t stream_id;
    struct virtio_video_stream_destroy *cmd =
        (struct virtio_video_stream_destroy *)vio_cmd->cmd_buf;
    enum v4l2_buf_type buftype;

    if (!v || !vio_cmd) {
        return;
    }

    stream_id = cmd->hdr.stream_id;

    g_debug("%s: stream_id=(%d)", __func__, stream_id);

    s = find_stream(v, stream_id);

    if (!s) {
        cmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_STREAM_ID;
        return;
    }

    if (s) {
        g_debug("%s: Found stream=0x%p", __func__, s);

        g_mutex_lock(&s->mutex);
        /* TODO assumes decoder */
        buftype = s->has_mplane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
            : V4L2_BUF_TYPE_VIDEO_OUTPUT;

        video_streamoff(s, buftype);

        /* signal worker thread */
        s->stream_state = STREAM_DESTROYING;
        g_cond_signal(&s->stream_cond);
        g_mutex_unlock(&s->mutex);

        /* wait for DESTROYED state */
        g_mutex_lock(&s->mutex);
        while (s->stream_state != STREAM_DESTROYED) {
            g_cond_wait(&s->stream_cond, &s->mutex);
        }

        /* stream worker thread now exited */

        enum virtio_video_mem_type mem_type = \
            get_queue_mem_type(s, VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
        /* deallocate the buffers */
        video_free_buffers(s->fd, buftype, get_v4l2_memory(mem_type));
        remove_all_resources(s, VIRTIO_VIDEO_QUEUE_TYPE_INPUT);

        buftype = s->has_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
            V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mem_type = get_queue_mem_type(s, VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
        video_free_buffers(s->fd, buftype, get_v4l2_memory(mem_type));
        remove_all_resources(s, VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);

        g_cond_clear(&s->stream_cond);

        v4l2_close(s->fd);

        v->streams = g_list_remove(v->streams, (gconstpointer) s);
        cmd->hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;
    }

    /* send response */
    vio_cmd->finished = true;
    send_ctrl_response_nodata(vio_cmd);
    g_mutex_unlock(&s->mutex);
    g_mutex_clear(&s->mutex);
    g_free(s);

    return;
}

struct virtio_video_get_control_resp_level {
    struct virtio_video_cmd_hdr hdr;
    struct virtio_video_control_val_level level;
};

struct virtio_video_get_control_resp_profile {
    struct virtio_video_cmd_hdr hdr;
    struct virtio_video_control_val_profile profile;
};

struct virtio_video_get_control_resp_bitrate {
    struct virtio_video_cmd_hdr hdr;
    struct virtio_video_control_val_bitrate bitrate;
};

static int
handle_query_control_cmd(struct VuVideo *v,
                         struct vu_video_ctrl_command *cmd)
{
    int ret;
    uint32_t v4l2_control;
    int32_t value;

    struct virtio_video_query_control_resp ctl_resp;

    struct virtio_video_query_control *qcmd =
        (struct virtio_video_query_control *)cmd->cmd_buf;

    g_debug("%s: type(0x%x) stream_id(%d) control(0x%x)", __func__,
            qcmd->hdr.type, qcmd->hdr.stream_id, le32toh(qcmd->control));

    v4l2_control = virtio_video_control_to_v4l2(le32toh(qcmd->control));
    if (!v4l2_control) {
        goto out_err_unlock;
    }


    ctl_resp.hdr.stream_id = qcmd->hdr.stream_id;
    ctl_resp.hdr.type = VIRTIO_VIDEO_RESP_OK_QUERY_CONTROL;

    switch (le32toh(qcmd->control)) {
    case VIRTIO_VIDEO_CONTROL_PROFILE:
        struct virtio_video_query_control_resp_profile *ctl_resp_profile;
        ctl_resp_profile = (void *)&ctl_resp +
            sizeof(struct virtio_video_query_control_resp_profile);

        g_debug("%s: VIRTIO_VIDEO_CONTROL_PROFILE", __func__);

        ret = v4l2_ioctl_query_control(v->v4l2_dev->fd, v4l2_control, &value);
        if (ret < 0) {
            g_printerr("v4l2_ioctl_query_control() failed\n");
            goto out_err_unlock;
        }

        ctl_resp_profile->num = htole32(value);

        cmd->finished = true;
        send_ctrl_response(cmd, (uint8_t *)&ctl_resp_profile,
                           sizeof(ctl_resp_profile));

        break;

    case VIRTIO_VIDEO_CONTROL_LEVEL:
        struct virtio_video_query_control_resp_level *ctl_resp_level;
        ctl_resp_level = (void *)&ctl_resp +
            sizeof(struct virtio_video_query_control_resp_level);

        g_debug("%s: VIRTIO_VIDEO_CONTROL_LEVEL", __func__);

        ret = v4l2_ioctl_query_control(v->v4l2_dev->fd, v4l2_control, &value);
        if (ret < 0) {
            g_printerr("v4l2_ioctl_query_control() failed\n");
            goto out_err_unlock;
        }

        ctl_resp_level->num = htole32(value);

        cmd->finished = true;
        send_ctrl_response(cmd, (uint8_t *)&ctl_resp_level,
                           sizeof(ctl_resp_profile));
        break;

    default:
        g_critical("Unknown control requested!");
        goto out_err_unlock;
        break;
    }

    return 0;

out_err_unlock:
    ctl_resp.hdr.stream_id = qcmd->hdr.stream_id;
    ctl_resp.hdr.type = VIRTIO_VIDEO_RESP_ERR_UNSUPPORTED_CONTROL;
    cmd->finished = true;
    send_ctrl_response(cmd, (uint8_t *)&ctl_resp,
                       sizeof(struct virtio_video_query_control_resp));
    return -EINVAL;
}

static int
handle_get_control_cmd(struct VuVideo *v, struct vu_video_ctrl_command *vio_cmd)
{
    int ret;
    uint32_t v4l2_control;
    int32_t value;

    struct virtio_video_get_control_resp ctl_resp_err;
    struct virtio_video_get_control_resp_level   ctl_resp_level;
    struct virtio_video_get_control_resp_profile ctl_resp_profile;
    struct virtio_video_get_control_resp_bitrate ctl_resp_bitrate;

    struct stream *s;

    struct virtio_video_get_control *cmd =
        (struct virtio_video_get_control *)vio_cmd->cmd_buf;

    g_debug("%s: type(0x%x) stream_id(%d) control(0x%x)", __func__,
            cmd->hdr.type, cmd->hdr.stream_id, le32toh(cmd->control));

    s = find_stream(v, cmd->hdr.stream_id);
    if (!s) {
        g_critical("%s: stream_id(%d) not found", __func__, cmd->hdr.stream_id);
        return -EINVAL;
    }

    g_mutex_lock(&s->mutex);

    v4l2_control = virtio_video_control_to_v4l2(le32toh(cmd->control));
    if (!v4l2_control) {
        goto out_err_unlock;
    }


    switch (le32toh(cmd->control)) {
    case VIRTIO_VIDEO_CONTROL_BITRATE:
        g_debug("%s: VIRTIO_VIDEO_CONTROL_BITRATE", __func__);

        ctl_resp_bitrate.hdr.stream_id = cmd->hdr.stream_id;
        ctl_resp_bitrate.hdr.type = VIRTIO_VIDEO_RESP_OK_GET_PARAMS;

        if (v->v4l2_dev->dev_type == STATEFUL_ENCODER) {
            ret = v4l2_ioctl_get_control(s->fd, v4l2_control, &value);
            if (ret < 0) {
                g_printerr("v4l2_ioctl_get_control() failed\n");
                goto out_err_unlock;
            }
            ctl_resp_bitrate.bitrate.bitrate = htole32(value);

        } else {
            g_debug("%s: CONTROL_BITRATE unsupported for decoders!", __func__);
            goto out_err_unlock;
        }

        vio_cmd->finished = true;
        send_ctrl_response(
            vio_cmd, (uint8_t *)&ctl_resp_bitrate,
            sizeof(struct virtio_video_get_control_resp_bitrate));
        break;

    case VIRTIO_VIDEO_CONTROL_PROFILE:
        g_debug("%s: VIRTIO_VIDEO_CONTROL_PROFILE", __func__);

        ctl_resp_profile.hdr.stream_id = cmd->hdr.stream_id;
        ctl_resp_profile.hdr.type = VIRTIO_VIDEO_RESP_OK_GET_PARAMS;

        ret = v4l2_ioctl_get_control(s->fd, v4l2_control, &value);
        if (ret < 0) {
            g_printerr("v4l2_ioctl_get_control() failed\n");
            goto out_err_unlock;
        }

        ctl_resp_profile.profile.profile = htole32(value);

        vio_cmd->finished = true;
        send_ctrl_response(
            vio_cmd, (uint8_t *)&ctl_resp_profile,
            sizeof(struct virtio_video_get_control_resp_profile));

        /*
         * TODO need to determine "in use" codec to (h264/vp8/vp9) to map to
         * v4l2 control for PROFILE?
         */

        break;

    case VIRTIO_VIDEO_CONTROL_LEVEL:
        g_debug("%s: VIRTIO_VIDEO_CONTROL_LEVEL", __func__);

        ctl_resp_level.hdr.stream_id = cmd->hdr.stream_id;
        ctl_resp_level.hdr.type = VIRTIO_VIDEO_RESP_OK_GET_PARAMS;

        ret = v4l2_ioctl_get_control(s->fd, v4l2_control, &value);
        if (ret < 0) {
            g_printerr("v4l2_ioctl_get_control() failed\n");
            goto out_err_unlock;
        }

        ctl_resp_level.level.level = htole32(value);

        vio_cmd->finished = true;
        send_ctrl_response(vio_cmd, (uint8_t *)&ctl_resp_level,
                           sizeof(struct virtio_video_get_control_resp_level));
        break;

    case VIRTIO_VIDEO_CONTROL_BITRATE_MODE:
    case VIRTIO_VIDEO_CONTROL_BITRATE_PEAK:
    case VIRTIO_VIDEO_CONTROL_PREPEND_SPSPPS_TO_IDR:
        g_info("Unsupported control requested");
        goto out_err_unlock;
        break;

    default:
        g_critical("Unknown control requested!");
        goto out_err_unlock;
        break;
    }

    g_mutex_unlock(&s->mutex);
    return 0;

out_err_unlock:
    ctl_resp_err.hdr.stream_id = cmd->hdr.stream_id;
    ctl_resp_err.hdr.type = VIRTIO_VIDEO_RESP_ERR_UNSUPPORTED_CONTROL;
    vio_cmd->finished = true;
    send_ctrl_response(vio_cmd, (uint8_t *)&ctl_resp_err,
                       sizeof(struct virtio_video_get_control_resp));
    g_mutex_unlock(&s->mutex);
    return -EINVAL;
}

static int
handle_query_capability_cmd(struct VuVideo *v,
                            struct vu_video_ctrl_command *cmd)
{
    GList *fmt_l;
    int ret;
    enum v4l2_buf_type buf_type;
    struct virtio_video_query_capability *qcmd =
        (struct virtio_video_query_capability *)cmd->cmd_buf;
    GByteArray *querycapresp;

    /* hdr bswapped already */
    g_debug("%s: type(0x%x) stream_id(%d) %s", __func__,
            qcmd->hdr.type, qcmd->hdr.stream_id,
            vio_queue_name(le32toh(qcmd->queue_type)));

    buf_type = get_v4l2_buf_type(le32toh(qcmd->queue_type),
                                 v->v4l2_dev->has_mplane);

    /* enumerate formats */
    ret = video_enum_formats(v->v4l2_dev, buf_type, &fmt_l, false);
    if (ret < 0) {
        g_printerr("video_enum_formats failed");
        return ret;
    }

    querycapresp = g_byte_array_new();
    querycapresp = create_query_cap_resp(qcmd, &fmt_l, querycapresp);
    cmd->finished = true;
    send_ctrl_response(cmd, querycapresp->data, querycapresp->len);

    video_free_formats(&fmt_l);
    g_byte_array_free(querycapresp, true);

    return 0;
}

static void
vv_process_cmd(VuVideo *video, struct vu_video_ctrl_command *cmd)
{
    switch (cmd->cmd_hdr->type) {
    case VIRTIO_VIDEO_CMD_QUERY_CAPABILITY:
        handle_query_capability_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_STREAM_CREATE:
        handle_stream_create_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_STREAM_DESTROY:
        handle_stream_destroy_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_STREAM_DRAIN:
        handle_stream_drain_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_RESOURCE_CREATE:
        handle_resource_create_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_RESOURCE_QUEUE:
        handle_resource_queue_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_RESOURCE_DESTROY_ALL:
        handle_resource_destroy_all_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_QUEUE_CLEAR:
        handle_queue_clear_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_GET_PARAMS_EXT:
        handle_get_params_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_SET_PARAMS_EXT:
        handle_set_params_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_QUERY_CONTROL:
        handle_query_control_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_GET_CONTROL:
        handle_get_control_cmd(video, cmd);
        break;
    case VIRTIO_VIDEO_CMD_SET_CONTROL:
        g_error("**** VIRTIO_VIDEO_CMD_SET_CONTROL unimplemented!");
        break;
    default:
        g_error("Unknown VIRTIO_VIDEO command!");
        break;
    }
}

/* for v3 virtio-video spec currently */
static void
video_handle_ctrl(VuDev *dev, int qidx)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);
    VuVideo *video = container_of(dev, VuVideo, dev.parent);
    size_t cmd_len, len, offset = 0;

    struct vu_video_ctrl_command *cmd;

    for (;;) {

        cmd = vu_queue_pop(dev, vq, sizeof(struct vu_video_ctrl_command));
        if (!cmd) {
            break;
        }

        cmd->vq = vq;
        cmd->error = 0;
        cmd->finished = false;
        cmd->dev = dev;

        cmd_len = iov_size(cmd->elem.out_sg, cmd->elem.out_num);
        cmd->cmd_buf = g_malloc0(cmd_len);
        len = iov_to_buf_full(cmd->elem.out_sg, cmd->elem.out_num,
                              offset, cmd->cmd_buf, cmd_len);

        if (len != cmd_len) {
            g_warning("%s: command size incorrect %zu vs %zu\n",
                      __func__, len, cmd_len);
        }

        /* header is first on every cmd struct */
        cmd->cmd_hdr = (struct virtio_video_cmd_hdr *) cmd->cmd_buf;
        /* bswap header */
        virtio_video_ctrl_hdr_letoh(cmd->cmd_hdr);
        g_debug("Received %s cmd", vv_cmd_to_string(cmd->cmd_hdr->type));
        vv_process_cmd(video, cmd);
    }
}

static void
video_queue_set_started(VuDev *dev, int qidx, bool started)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);

    g_debug("queue started %d:%d\n", qidx, started);

    switch (qidx) {
    case 0:
        vu_set_queue_handler(dev, vq, started ? video_handle_ctrl : NULL);
        break;
    default:
        break;
    }
}

/*
 * video_process_msg: process messages of vhost-user interface
 *
 * Any that are not handled here are processed by the libvhost library
 * itself.
 */
static int video_process_msg(VuDev *dev, VhostUserMsg *msg, int *do_reply)
{
    VuVideo *r = container_of(dev, VuVideo, dev.parent);

    g_debug("%s: msg %d", __func__, msg->request);

    switch (msg->request) {
    case VHOST_USER_NONE:
        g_main_loop_quit(r->loop);
        return 1;
    default:
        return 0;
    }
}

static const VuDevIface vuiface = {
    .set_features = video_set_features,
    .get_features = video_get_features,
    .queue_set_started = video_queue_set_started,
    .process_msg = video_process_msg,
    .get_config = video_get_config,
    .set_config = video_set_config,
};

static void video_destroy(VuVideo *v)
{
    vug_deinit(&v->dev);
    if (socket_path) {
        unlink(socket_path);
    }
    vuvbm_device_destroy(v->bm_dev);
    v4l2_backend_free(v->v4l2_dev);
}

/* Print vhost-user.json backend program capabilities */
static void print_capabilities(void)
{
    printf("{\n");
    printf("  \"type\": \"misc\"\n");
    printf("}\n");
}

static gboolean hangup(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *) user_data;
    g_info("%s: caught hangup/quit signal, quitting main loop", __func__);
    g_main_loop_quit(loop);
    return true;
}

int main(int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    g_autoptr(GSocket) socket = NULL;
    VuVideo video = {  };

    context = g_option_context_new("vhost-user emulation of video device");
    g_option_context_add_main_entries(context, options, "vhost-user-video");
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        exit(1);
    }

    g_option_context_free(context);

    if (print_cap) {
        print_capabilities();
        exit(0);
    }

    if (!socket_path && socket_fd < 0) {
        g_printerr("Please specify either --fd or --socket-path\n");
        exit(EXIT_FAILURE);
    }

    if (verbose || debug) {
        g_log_set_handler(NULL, G_LOG_LEVEL_MASK, g_log_default_handler, NULL);
        if (debug) {
            g_setenv("G_MESSAGES_DEBUG", "all", true);
        }
    } else {
        g_log_set_handler(NULL,
                          G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL
                          | G_LOG_LEVEL_ERROR,
                          g_log_default_handler, NULL);
    }

    /*
     * Open the v4l2 device and enumerate supported formats.
     * Use this to determine whether it is a stateful encoder/decoder.
     */
    if (!v4l2_path || !g_file_test(v4l2_path, G_FILE_TEST_EXISTS)) {
        g_printerr("Please specify a valid --v4l2-device\n");
        exit(EXIT_FAILURE);
    } else {
        video.v4l2_dev = v4l2_backend_init(v4l2_path);
        if (!video.v4l2_dev) {
            g_printerr("v4l2 backend init failed!\n");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Create a new Buffer Memory device to handle DMA buffers.
     */
    video.bm_dev = g_new0(struct vuvbm_device, 1);

    /*
     * Now create a vhost-user socket that we will receive messages
     * on. Once we have our handler set up we can enter the glib main
     * loop.
     */
    if (socket_path) {
        g_autoptr(GSocketAddress) addr = g_unix_socket_address_new(socket_path);
        g_autoptr(GSocket) bind_socket =
            g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
                         G_SOCKET_PROTOCOL_DEFAULT, &error);

        if (!g_socket_bind(bind_socket, addr, false, &error)) {
            g_printerr("Failed to bind to socket at %s (%s).\n",
                       socket_path, error->message);
            exit(EXIT_FAILURE);
        }
        if (!g_socket_listen(bind_socket, &error)) {
            g_printerr("Failed to listen on socket %s (%s).\n",
                       socket_path, error->message);
        }
        g_message("awaiting connection to %s", socket_path);
        socket = g_socket_accept(bind_socket, NULL, &error);
        if (!socket) {
            g_printerr("Failed to accept on socket %s (%s).\n",
                       socket_path, error->message);
        }
    } else {
        socket = g_socket_new_from_fd(socket_fd, &error);
        if (!socket) {
            g_printerr("Failed to connect to FD %d (%s).\n",
                       socket_fd, error->message);
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Create the main loop first so all the various sources can be
     * added. As well as catching signals we need to ensure vug_init
     * can add it's GSource watches.
     */

    video.loop = g_main_loop_new(NULL, FALSE);
    /* catch exit signals */
    g_unix_signal_add(SIGHUP, hangup, video.loop);
    g_unix_signal_add(SIGINT, hangup, video.loop);

    if (!vug_init(&video.dev, VHOST_USER_VIDEO_MAX_QUEUES,
                  g_socket_get_fd(socket),
                  video_panic, &vuiface)) {
        g_printerr("Failed to initialize libvhost-user-glib.\n");
        exit(EXIT_FAILURE);
    }

    g_message("entering main loop, awaiting messages");
    g_main_loop_run(video.loop);
    g_message("finished main loop, cleaning up");

    g_main_loop_unref(video.loop);
    video_destroy(&video);
}
