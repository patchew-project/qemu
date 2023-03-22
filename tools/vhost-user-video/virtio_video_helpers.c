// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-video helpers
 *
 * Copyright Red Hat, Inc. 2023
 * Copyright Linaro 2021
 * Copyright 2019 OpenSynergy GmbH.
 *
 * Authors:
 *      Peter Griffin <peter.griffin@linaro.org>
 *      Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "standard-headers/linux/virtio_video.h"
#include <linux/videodev2.h>
#include "v4l2_backend.h"
#include "virtio_video_helpers.h"

struct virtio_video_convert_table {
    uint32_t virtio_value;
    uint32_t v4l2_value;
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct virtio_video_convert_table level_table[] = {
    { VIRTIO_VIDEO_LEVEL_H264_1_0, V4L2_MPEG_VIDEO_H264_LEVEL_1_0 },
    { VIRTIO_VIDEO_LEVEL_H264_1_1, V4L2_MPEG_VIDEO_H264_LEVEL_1_1 },
    { VIRTIO_VIDEO_LEVEL_H264_1_2, V4L2_MPEG_VIDEO_H264_LEVEL_1_2 },
    { VIRTIO_VIDEO_LEVEL_H264_1_3, V4L2_MPEG_VIDEO_H264_LEVEL_1_3 },
    { VIRTIO_VIDEO_LEVEL_H264_2_0, V4L2_MPEG_VIDEO_H264_LEVEL_2_0 },
    { VIRTIO_VIDEO_LEVEL_H264_2_1, V4L2_MPEG_VIDEO_H264_LEVEL_2_1 },
    { VIRTIO_VIDEO_LEVEL_H264_2_2, V4L2_MPEG_VIDEO_H264_LEVEL_2_2 },
    { VIRTIO_VIDEO_LEVEL_H264_3_0, V4L2_MPEG_VIDEO_H264_LEVEL_3_0 },
    { VIRTIO_VIDEO_LEVEL_H264_3_1, V4L2_MPEG_VIDEO_H264_LEVEL_3_1 },
    { VIRTIO_VIDEO_LEVEL_H264_3_2, V4L2_MPEG_VIDEO_H264_LEVEL_3_2 },
    { VIRTIO_VIDEO_LEVEL_H264_4_0, V4L2_MPEG_VIDEO_H264_LEVEL_4_0 },
    { VIRTIO_VIDEO_LEVEL_H264_4_1, V4L2_MPEG_VIDEO_H264_LEVEL_4_1 },
    { VIRTIO_VIDEO_LEVEL_H264_4_2, V4L2_MPEG_VIDEO_H264_LEVEL_4_2 },
    { VIRTIO_VIDEO_LEVEL_H264_5_0, V4L2_MPEG_VIDEO_H264_LEVEL_5_0 },
    { VIRTIO_VIDEO_LEVEL_H264_5_1, V4L2_MPEG_VIDEO_H264_LEVEL_5_1 },
    { 0 },
};

uint32_t virtio_video_level_to_v4l2(uint32_t level)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(level_table); idx++) {
        if (level_table[idx].virtio_value == level) {
            return level_table[idx].v4l2_value;
        }
    }

    return 0;
}

uint32_t virtio_video_v4l2_level_to_virtio(uint32_t v4l2_level)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(level_table); idx++) {
        if (level_table[idx].v4l2_value == v4l2_level) {
            return level_table[idx].virtio_value;
        }
    }

    return 0;
}

static struct virtio_video_convert_table profile_table[] = {
    { VIRTIO_VIDEO_PROFILE_H264_BASELINE,
      V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE },
    { VIRTIO_VIDEO_PROFILE_H264_MAIN, V4L2_MPEG_VIDEO_H264_PROFILE_MAIN },
    { VIRTIO_VIDEO_PROFILE_H264_EXTENDED,
      V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED },
    { VIRTIO_VIDEO_PROFILE_H264_HIGH, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH },
    { VIRTIO_VIDEO_PROFILE_H264_HIGH10PROFILE,
      V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10 },
    { VIRTIO_VIDEO_PROFILE_H264_HIGH422PROFILE,
      V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422},
    { VIRTIO_VIDEO_PROFILE_H264_HIGH444PREDICTIVEPROFILE,
      V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE },
    { VIRTIO_VIDEO_PROFILE_H264_SCALABLEBASELINE,
      V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_BASELINE },
    { VIRTIO_VIDEO_PROFILE_H264_SCALABLEHIGH,
      V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH },
    { VIRTIO_VIDEO_PROFILE_H264_STEREOHIGH,
      V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH },
    { VIRTIO_VIDEO_PROFILE_H264_MULTIVIEWHIGH,
      V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH },
    { 0 },
};

uint32_t virtio_video_profile_to_v4l2(uint32_t profile)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(profile_table); idx++) {
        if (profile_table[idx].virtio_value == profile) {
            return profile_table[idx].v4l2_value;
        }
    }

    return 0;
}

uint32_t virtio_video_v4l2_profile_to_virtio(uint32_t v4l2_profile)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(profile_table); idx++) {
        if (profile_table[idx].v4l2_value == v4l2_profile) {
            return profile_table[idx].virtio_value;
        }
    }

    return 0;
}


static struct virtio_video_convert_table format_table[] = {
    { VIRTIO_VIDEO_FORMAT_ARGB8888, V4L2_PIX_FMT_ARGB32 },
    { VIRTIO_VIDEO_FORMAT_BGRA8888, V4L2_PIX_FMT_ABGR32 },
    { VIRTIO_VIDEO_FORMAT_NV12, V4L2_PIX_FMT_NV12 },
    { VIRTIO_VIDEO_FORMAT_YUV420, V4L2_PIX_FMT_YUV420 },
    { VIRTIO_VIDEO_FORMAT_YVU420, V4L2_PIX_FMT_YVU420 },
    { VIRTIO_VIDEO_FORMAT_MPEG2, V4L2_PIX_FMT_MPEG2 },
    { VIRTIO_VIDEO_FORMAT_MPEG4, V4L2_PIX_FMT_MPEG4 },
    { VIRTIO_VIDEO_FORMAT_H264, V4L2_PIX_FMT_H264 },
    { VIRTIO_VIDEO_FORMAT_HEVC, V4L2_PIX_FMT_HEVC },
    { VIRTIO_VIDEO_FORMAT_VP8, V4L2_PIX_FMT_VP8 },
    { VIRTIO_VIDEO_FORMAT_VP9, V4L2_PIX_FMT_VP9 },
    { VIRTIO_VIDEO_FORMAT_FWHT, V4L2_PIX_FMT_FWHT },
    { 0 },
};

uint32_t virtio_video_format_to_v4l2(uint32_t format)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(format_table); idx++) {
        if (format_table[idx].virtio_value == format) {
            return format_table[idx].v4l2_value;
        }
    }

    return 0;
}

uint32_t virtio_video_v4l2_format_to_virtio(uint32_t v4l2_format)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(format_table); idx++) {
        if (format_table[idx].v4l2_value == v4l2_format) {
            return format_table[idx].virtio_value;
        }
    }

    return 0;
}

/*
 * TODO FIXME PROFILE and LEVEL seem wrong here as tied to H264 codec.
 * V4L2_CID_MPEG_VIDEO_VP9_PROFILE
 * e.g. https://elixir.bootlin.com/linux/v5.12.1/source/
 * include/uapi/linux/v4l2-controls.h#L669
 */
static struct virtio_video_convert_table control_table[] = {
    { VIRTIO_VIDEO_CONTROL_BITRATE, V4L2_CID_MPEG_VIDEO_BITRATE },
    { VIRTIO_VIDEO_CONTROL_BITRATE_PEAK, V4L2_CID_MPEG_VIDEO_BITRATE_PEAK },
    { VIRTIO_VIDEO_CONTROL_BITRATE_MODE, V4L2_CID_MPEG_VIDEO_BITRATE_MODE },
    { VIRTIO_VIDEO_CONTROL_PROFILE, V4L2_CID_MPEG_VIDEO_H264_PROFILE },
    { VIRTIO_VIDEO_CONTROL_LEVEL, V4L2_CID_MPEG_VIDEO_H264_LEVEL },
    { VIRTIO_VIDEO_CONTROL_FORCE_KEYFRAME,
            V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME },
    { VIRTIO_VIDEO_CONTROL_PREPEND_SPSPPS_TO_IDR,
            V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR },
    { 0 },
};

uint32_t virtio_video_control_to_v4l2(uint32_t control)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(control_table); idx++) {
        if (control_table[idx].virtio_value == control) {
            return control_table[idx].v4l2_value;
        }
    }

    return 0;
}

uint32_t virtio_video_v4l2_control_to_virtio(uint32_t v4l2_control)
{
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(control_table); idx++) {
        if (control_table[idx].v4l2_value == v4l2_control) {
            return control_table[idx].virtio_value;
        }
    }

    return 0;
}

/* new helper functions (not from Linux frontend driver) */

const char *vio_queue_name(enum virtio_video_queue_type queue)
{
    if (queue == VIRTIO_VIDEO_QUEUE_TYPE_INPUT) {
        return "Queue Input";
    }
    if (queue == VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT) {
        return "Queue Output";
    }

    return "Queue type unknown";
}


__le64 virtio_fmtdesc_generate_mask(GList **p_list)
{
    uint64_t mask = 0;
    unsigned int bit = 0;
    GList *l;

    for (l = *p_list; l != NULL; l = l->next) {
        mask |= (1 << bit);
        bit++;
    }
    g_debug("%s: mask(0x%lx)\n", __func__, mask);

    return mask;
}

/* vio_codedformat endian swapped by upper level */

int v4l2_stream_create(struct v4l2_device *dev, uint32_t vio_codedformat,
                       struct stream *s)
{
    enum v4l2_buf_type buf_type;
    uint32_t v4l2_pixformat;
    int ret;

    /* buf type for coded format depends on device type */
    if (dev->dev_type & STATEFUL_DECODER) {
        buf_type = dev->has_mplane ?  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
            : V4L2_BUF_TYPE_VIDEO_OUTPUT;

    } else if (dev->dev_type & STATEFUL_ENCODER) {
        buf_type = dev->has_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
            V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        g_critical("Unknown device type %d!", dev->dev_type);
        return -EINVAL;
    }

    s->fd = v4l2_open(dev->devname);
    if (s->fd < 0) {
        g_printerr("Error opening device %s: %s (%d).\n", dev->devname,
                   g_strerror(errno), errno);
    }

    v4l2_pixformat = virtio_video_format_to_v4l2(vio_codedformat);
    if (v4l2_pixformat == 0) {
        g_error("%s: virtio to v4l2 format translation failed!", __func__);
        ret = -EINVAL;
        return ret;
    }

    /* set the requested coded format */
    ret = v4l2_set_pixel_format(s->fd, buf_type, v4l2_pixformat);
    if (ret < 0) {
        g_printerr("%s: v4l2_video_set_pixel_format() failed", __func__);
    }

    return ret;
}

void v4l2_to_virtio_fmtdesc(struct v4l2_device *dev,
                            struct video_format *vid_fmt,
                            enum v4l2_buf_type type)
{
    struct v4l2_fmtdesc *v4l2_fmtdsc = &vid_fmt->fmt;
    struct virtio_video_format_desc *virtio_fmtdesc = &vid_fmt->desc;
    enum v4l2_buf_type buftype = V4L2_BUF_TYPE_PRIVATE;
    int ret;

    if (!vid_fmt) {
        return;
    }

    virtio_fmtdesc->format =
        htole32(virtio_video_v4l2_format_to_virtio(v4l2_fmtdsc->pixelformat));

    /*
     * To generate the mask we need to check the FORMAT is already set.
     * before we enumerate the other queue to generate the mask
     */

    ret = v4l2_set_pixel_format(dev->fd, type, vid_fmt->fmt.pixelformat);
    if (ret < 0) {
        g_printerr("%s: v4l2_video_get_format() failed\n", __func__);
    }

    /* enumerate formats on the other queue now the format is set */
    GList *vid_fmts_l = NULL;

    if (V4L2_TYPE_IS_OUTPUT(type)) {
        buftype = V4L2_TYPE_IS_MULTIPLANAR(type) ?
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
            V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    if (V4L2_TYPE_IS_CAPTURE(type)) {
        buftype = V4L2_TYPE_IS_MULTIPLANAR(type) ?
            V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
            V4L2_BUF_TYPE_VIDEO_OUTPUT;
    }

    ret = video_enum_formats(dev, buftype, &vid_fmts_l, true);

    /*
     * generate the capability mask. bitset represents the supported
     * combinations of input and output formats.
     */

    virtio_fmtdesc->mask = htole64(virtio_fmtdesc_generate_mask(&vid_fmts_l));

    virtio_fmtdesc->planes_layout =
        htole32(VIRTIO_VIDEO_PLANES_LAYOUT_SINGLE_BUFFER);

    /* TODO need to set plane_align */
    if ((!v4l2_fmtdsc->flags & V4L2_FMT_FLAG_COMPRESSED) &&
        (le32toh(virtio_fmtdesc->planes_layout) &
         VIRTIO_VIDEO_PLANES_LAYOUT_SINGLE_BUFFER)) {
        g_critical("%s: TODO need to set plane_align field", __func__);
    }

    virtio_fmtdesc->num_frames = htole32(g_list_length(vid_fmt->vid_fmt_frm_l));

    video_free_formats(&vid_fmts_l);
}

void v4l2_to_virtio_video_params(struct v4l2_device *dev,
                                 struct v4l2_format *fmt,
                                 struct v4l2_selection *sel,
                                 struct virtio_video_get_params_resp *resp)
{
    struct virtio_video_params *vid_params = &resp->params;
    int i;

    /* min/max_buffers default (taken from crosvm) */
    vid_params->min_buffers = htole32(1);
    vid_params->max_buffers = htole32(32);

    if (V4L2_TYPE_IS_MULTIPLANAR(fmt->type)) {
        vid_params->format =
            virtio_video_v4l2_format_to_virtio(fmt->fmt.pix.pixelformat);
        vid_params->frame_width = htole32(fmt->fmt.pix_mp.width);
        vid_params->frame_height = htole32(fmt->fmt.pix_mp.height);

        vid_params->num_planes = htole32(fmt->fmt.pix_mp.num_planes);

        for (i = 0; i < fmt->fmt.pix_mp.num_planes; i++) {
            vid_params->plane_formats[i].stride = \
                htole32(fmt->fmt.pix_mp.plane_fmt[i].bytesperline);

            vid_params->plane_formats[i].plane_size = \
                htole32(fmt->fmt.pix_mp.plane_fmt[i].sizeimage);

            g_debug(" ** Stride %u, buffer size %u\n",
                    fmt->fmt.pix_mp.plane_fmt[i].bytesperline,
                    fmt->fmt.pix_mp.plane_fmt[i].sizeimage);
        }
    } else if (V4L2_TYPE_IS_SINGLEPLANAR(fmt->type)) {
        vid_params->format =
            virtio_video_v4l2_format_to_virtio(fmt->fmt.pix.pixelformat);
        vid_params->frame_width = htole32(fmt->fmt.pix.width);
        vid_params->frame_height = htole32(fmt->fmt.pix.height);
        vid_params->num_planes = htole32(1);

        vid_params->plane_formats[0].stride = \
            htole32(fmt->fmt.pix.bytesperline);

        vid_params->plane_formats[0].plane_size = \
            htole32(fmt->fmt.pix.sizeimage);
    }

    /* cropping rectangle */
    if (V4L2_TYPE_IS_CAPTURE(fmt->type)) {
        vid_params->crop.left = htole32(sel->r.left);
        vid_params->crop.top = htole32(sel->r.top);
        vid_params->crop.width = htole32(sel->r.width);
        vid_params->crop.height = htole32(sel->r.height);

        g_debug("%s: crop: left=(%d) top=(%d) width=(%d) height=(%d)",
                __func__, sel->r.left, sel->r.top, sel->r.width, sel->r.height);
    }

    /* TODO frame_rate field for encoder */
}

void v4l2_to_virtio_event(struct v4l2_event *ev,
                          struct virtio_video_event *vio_ev)
{
    g_debug("%s: %ld.%06ld: event %u, pending %u",
            __func__, ev->timestamp.tv_sec, ev->timestamp.tv_nsec / 1000,
            ev->sequence, ev->pending);
    /* Reset event type */
    vio_ev->event_type = 0x0;
    switch (ev->type) {
    case V4L2_EVENT_VSYNC:
        g_debug("vsync\n");
        break;
    case V4L2_EVENT_EOS:
        g_debug("eos\n");
        break;
    case V4L2_EVENT_CTRL:
        g_debug("eos\n");
        break;
    case V4L2_EVENT_FRAME_SYNC:
        g_debug("frame_sync %d\n", ev->u.frame_sync.frame_sequence);
        break;
    case V4L2_EVENT_SOURCE_CHANGE:
        g_debug("source_change!: pad/input=%d changes: %x\n"
                , ev->id, ev->u.src_change.changes);

        vio_ev->event_type =
            htole32(VIRTIO_VIDEO_EVENT_DECODER_RESOLUTION_CHANGED);
        /* TODO need proper mapping from v4l2 streamid to virtio streamid */
        vio_ev->stream_id = htole32(ev->id) + 1;
        break;
    case V4L2_EVENT_MOTION_DET:
        if (ev->u.motion_det.flags & V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ) {
            g_debug("motion_det frame %d, regions 0x%x\n",
                    ev->u.motion_det.frame_sequence,
                    ev->u.motion_det.region_mask);
        } else {
            g_debug("motion_det regions 0x%x\n", ev->u.motion_det.region_mask);
        }
        break;
    default:
        if (ev->type >= V4L2_EVENT_PRIVATE_START) {
            g_debug("unknown private event (%08x)\n", ev->type);
        } else {
            g_debug("unknown event (%08x)\n", ev->type);
        }
        break;
    }
}
