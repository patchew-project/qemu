/*
 * virtio-video video v4l2 backend
 *
 * The purpose of this backend is to interface with
 * v4l2 stateful encoder and decoder devices in the kernel.
 *
 * v4l2 stateless devices are NOT supported currently.
 *
 * Some v4l2 helper functions taken from yatva
 *
 * Copyright (c) 2023 Red Hat, Inc.
 * Copyright (c) 2021 Linaro Ltd
 * Copyright (C) 2005-2010 Laurent Pinchart
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include <linux/videodev2.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include "virtio_video_helpers.h"
#include "v4l2_backend.h"
#include "standard-headers/linux/virtio_video.h"
#include "vuvideo.h"

#define V4L2_TYPE_IS_META(type)             \
    ((type) == V4L2_BUF_TYPE_META_CAPTURE   \
     || (type) == V4L2_BUF_TYPE_META_OUTPUT)

/* Function prototypes */
static const struct v4l2_format_info *
v4l2_format_by_fourcc(unsigned int fourcc);
static const char *v4l2_format_name(unsigned int fourcc);
static const char *v4l2_buf_type_name(enum v4l2_buf_type type);
static const char *v4l2_field_name(enum v4l2_field field);

static int video_enum_frame_intervals(struct v4l2_device *dev,
                                      __u32 pixelformat,
                                      unsigned int width, unsigned int height,
                                      GList **p_vid_fmt_frm_rate_l);

static int video_enum_frame_sizes(struct v4l2_device *dev, __u32 pixelformat,
                                  GList **p_vid_fmt_frm_l);
static int video_querycap(struct v4l2_device *dev);
static GByteArray *iterate_frame_rate_list(GByteArray *resp,
                                            GList *frm_rate_l);
static GByteArray *iterate_format_frame_list(GByteArray *resp,
                                             GList *fmt_frm_l);
static GByteArray *iterate_format_desc_list(GByteArray *resp,
                                            GList *fmt_desc_l);
static void convert_to_timeval(uint64_t timestamp, struct timeval *t);

/* v4l2 to str tables & helpers taken from yavta to make prettier logs */
static struct v4l2_format_info {
    const char *name;
    unsigned int fourcc;
    unsigned char n_planes;
} pixel_formats[] = {
    { "RGB332", V4L2_PIX_FMT_RGB332, 1 },
    { "RGB444", V4L2_PIX_FMT_RGB444, 1 },
    { "ARGB444", V4L2_PIX_FMT_ARGB444, 1 },
    { "XRGB444", V4L2_PIX_FMT_XRGB444, 1 },
    { "RGB555", V4L2_PIX_FMT_RGB555, 1 },
    { "ARGB555", V4L2_PIX_FMT_ARGB555, 1 },
    { "XRGB555", V4L2_PIX_FMT_XRGB555, 1 },
    { "RGB565", V4L2_PIX_FMT_RGB565, 1 },
    { "RGB555X", V4L2_PIX_FMT_RGB555X, 1 },
    { "RGB565X", V4L2_PIX_FMT_RGB565X, 1 },
    { "BGR666", V4L2_PIX_FMT_BGR666, 1 },
    { "BGR24", V4L2_PIX_FMT_BGR24, 1 },
    { "RGB24", V4L2_PIX_FMT_RGB24, 1 },
    { "BGR32", V4L2_PIX_FMT_BGR32, 1 },
    { "ABGR32", V4L2_PIX_FMT_ABGR32, 1 },
    { "XBGR32", V4L2_PIX_FMT_XBGR32, 1 },
    { "RGB32", V4L2_PIX_FMT_RGB32, 1 },
    { "ARGB32", V4L2_PIX_FMT_ARGB32, 1 },
    { "XRGB32", V4L2_PIX_FMT_XRGB32, 1 },
    { "HSV24", V4L2_PIX_FMT_HSV24, 1 },
    { "HSV32", V4L2_PIX_FMT_HSV32, 1 },
    { "Y8", V4L2_PIX_FMT_GREY, 1 },
    { "Y10", V4L2_PIX_FMT_Y10, 1 },
    { "Y12", V4L2_PIX_FMT_Y12, 1 },
    { "Y16", V4L2_PIX_FMT_Y16, 1 },
    { "UYVY", V4L2_PIX_FMT_UYVY, 1 },
    { "VYUY", V4L2_PIX_FMT_VYUY, 1 },
    { "YUYV", V4L2_PIX_FMT_YUYV, 1 },
    { "YVYU", V4L2_PIX_FMT_YVYU, 1 },
    { "NV12", V4L2_PIX_FMT_NV12, 1 },
    { "NV12M", V4L2_PIX_FMT_NV12M, 2 },
    { "NV21", V4L2_PIX_FMT_NV21, 1 },
    { "NV21M", V4L2_PIX_FMT_NV21M, 2 },
    { "NV16", V4L2_PIX_FMT_NV16, 1 },
    { "NV16M", V4L2_PIX_FMT_NV16M, 2 },
    { "NV61", V4L2_PIX_FMT_NV61, 1 },
    { "NV61M", V4L2_PIX_FMT_NV61M, 2 },
    { "NV24", V4L2_PIX_FMT_NV24, 1 },
    { "NV42", V4L2_PIX_FMT_NV42, 1 },
    { "YU12", V4L2_PIX_FMT_YVU420, 1},
    { "YUV420M", V4L2_PIX_FMT_YUV420M, 3 },
    { "YUV422M", V4L2_PIX_FMT_YUV422M, 3 },
    { "YUV444M", V4L2_PIX_FMT_YUV444M, 3 },
    { "YVU420M", V4L2_PIX_FMT_YVU420M, 3 },
    { "YVU422M", V4L2_PIX_FMT_YVU422M, 3 },
    { "YVU444M", V4L2_PIX_FMT_YVU444M, 3 },
    { "SBGGR8", V4L2_PIX_FMT_SBGGR8, 1 },
    { "SGBRG8", V4L2_PIX_FMT_SGBRG8, 1 },
    { "SGRBG8", V4L2_PIX_FMT_SGRBG8, 1 },
    { "SRGGB8", V4L2_PIX_FMT_SRGGB8, 1 },
    { "SBGGR10_DPCM8", V4L2_PIX_FMT_SBGGR10DPCM8, 1 },
    { "SGBRG10_DPCM8", V4L2_PIX_FMT_SGBRG10DPCM8, 1 },
    { "SGRBG10_DPCM8", V4L2_PIX_FMT_SGRBG10DPCM8, 1 },
    { "SRGGB10_DPCM8", V4L2_PIX_FMT_SRGGB10DPCM8, 1 },
    { "SBGGR10", V4L2_PIX_FMT_SBGGR10, 1 },
    { "SGBRG10", V4L2_PIX_FMT_SGBRG10, 1 },
    { "SGRBG10", V4L2_PIX_FMT_SGRBG10, 1 },
    { "SRGGB10", V4L2_PIX_FMT_SRGGB10, 1 },
    { "SBGGR10P", V4L2_PIX_FMT_SBGGR10P, 1 },
    { "SGBRG10P", V4L2_PIX_FMT_SGBRG10P, 1 },
    { "SGRBG10P", V4L2_PIX_FMT_SGRBG10P, 1 },
    { "SRGGB10P", V4L2_PIX_FMT_SRGGB10P, 1 },
    { "SBGGR12", V4L2_PIX_FMT_SBGGR12, 1 },
    { "SGBRG12", V4L2_PIX_FMT_SGBRG12, 1 },
    { "SGRBG12", V4L2_PIX_FMT_SGRBG12, 1 },
    { "SRGGB12", V4L2_PIX_FMT_SRGGB12, 1 },
    { "IPU3_SBGGR10", V4L2_PIX_FMT_IPU3_SBGGR10, 1 },
    { "IPU3_SGBRG10", V4L2_PIX_FMT_IPU3_SGBRG10, 1 },
    { "IPU3_SGRBG10", V4L2_PIX_FMT_IPU3_SGRBG10, 1 },
    { "IPU3_SRGGB10", V4L2_PIX_FMT_IPU3_SRGGB10, 1 },
    { "DV", V4L2_PIX_FMT_DV, 1 },
    { "MJPEG", V4L2_PIX_FMT_MJPEG, 1 },
    { "MPEG", V4L2_PIX_FMT_MPEG, 1 },
    { "FWHT", V4L2_PIX_FMT_FWHT, 1 },
};

static const struct v4l2_format_info *v4l2_format_by_fourcc(unsigned int fourcc)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(pixel_formats); ++i) {
        if (pixel_formats[i].fourcc == fourcc) {
            return &pixel_formats[i];
        }
    }

    return NULL;
}

static const char *v4l2_format_name(unsigned int fourcc)
{
    const struct v4l2_format_info *info;
    static char name[5];
    unsigned int i;

    info = v4l2_format_by_fourcc(fourcc);
    if (info) {
        return info->name;
    }

    for (i = 0; i < 4; ++i) {
        name[i] = fourcc & 0xff;
        fourcc >>= 8;
    }

    name[4] = '\0';
    return name;
}

static struct {
    enum v4l2_buf_type type;
    bool supported;
    const char *name;
} buf_types_array[] = {
    { V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, "Video capture mplanes", },
    { V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 1, "Video output mplanes", },
    { V4L2_BUF_TYPE_VIDEO_CAPTURE, 1, "Video capture", },
    { V4L2_BUF_TYPE_VIDEO_OUTPUT, 1, "Video output", },
    { V4L2_BUF_TYPE_VIDEO_OVERLAY, 0, "Video overlay", },
    { V4L2_BUF_TYPE_META_CAPTURE, 0, "Meta-data capture", },
    { V4L2_BUF_TYPE_META_OUTPUT, 0, "Meta-data output", },
};

static const char *v4l2_buf_type_name(enum v4l2_buf_type type)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(buf_types_array); ++i) {
        if (buf_types_array[i].type == type) {
            return buf_types_array[i].name;
        }
    }

    if (type & V4L2_BUF_TYPE_PRIVATE) {
        return "Private";
    } else {
        return "Unknown";
    }
}

static const struct {
    const char *name;
    enum v4l2_field field;
} fields[] = {
    { "any", V4L2_FIELD_ANY },
    { "none", V4L2_FIELD_NONE },
    { "top", V4L2_FIELD_TOP },
    { "bottom", V4L2_FIELD_BOTTOM },
    { "interlaced", V4L2_FIELD_INTERLACED },
    { "seq-tb", V4L2_FIELD_SEQ_TB },
    { "seq-bt", V4L2_FIELD_SEQ_BT },
    { "alternate", V4L2_FIELD_ALTERNATE },
    { "interlaced-tb", V4L2_FIELD_INTERLACED_TB },
    { "interlaced-bt", V4L2_FIELD_INTERLACED_BT },
};

static const char *v4l2_field_name(enum v4l2_field field)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(fields); ++i) {
        if (fields[i].field == field) {
            return fields[i].name;
        }
    }

    return "unknown";
}

int v4l2_open(const gchar *devname)
{
    int fd;

    if (!devname) {
        return -EINVAL;
    }

    fd = open(devname, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        g_printerr("Error opening device %s: %s (%d)\n", devname,
                   g_strerror(errno), errno);
        return fd;
    }

    g_print("Device %s opened fd(%d).\n", devname, fd);

    return fd;
}

int v4l2_close(int fd)
{
    int ret;
    ret = close(fd);

    if (ret < 0) {
        g_printerr("Error closing device: %s (%d)\n", g_strerror(errno), errno);
    }
    return ret;
}

static int video_enum_frame_intervals(struct v4l2_device *dev,
                                      __u32 pixelformat,
                                      unsigned int width, unsigned int height,
                                      GList **p_vid_fmt_frm_rate_l)
{
    struct v4l2_frmivalenum ival;
    GList *vid_fmt_frm_rate_l = NULL;
    struct video_format_frame_rates *fmt_frm_rate;
    unsigned int i;
    int ret = 0;

    for (i = 0; ; ++i) {
        memset(&ival, 0, sizeof ival);
        ival.index = i;
        ival.pixel_format = pixelformat;
        ival.width = width;
        ival.height = height;
        ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival);
        if (ret < 0) {
            if (errno == EINVAL) /* EINVAL means no more frame intervals */
                ret = 0;
            else
                g_printerr("VIDIOC_ENUM_FRAMEINTERVALS failed: %s (%d)\n",
                           g_strerror(errno), errno);
            break;
        }

        /* driver sanity checks */
        if (i != ival.index)
            g_printerr("Warning: driver returned wrong ival index "
                       "%u.\n", ival.index);
        if (pixelformat != ival.pixel_format)
            g_printerr("Warning: driver returned wrong ival pixel "
                       "format %08x.\n", ival.pixel_format);
        if (width != ival.width)
            g_printerr("Warning: driver returned wrong ival width "
                       "%u.\n", ival.width);
        if (height != ival.height)
            g_printerr("Warning: driver returned wrong ival height "
                       "%u.\n", ival.height);

        if (i != 0) {
            g_print(", ");
        }

        /* allocate video_format_frame */
        fmt_frm_rate = g_new0(struct video_format_frame_rates, 1);
        /* keep a copy of v4l2 frmsizeenum struct */
        memcpy(&fmt_frm_rate->v4l_ival, &ival,
               sizeof(struct v4l2_frmivalenum));
        vid_fmt_frm_rate_l =
            g_list_append(vid_fmt_frm_rate_l, fmt_frm_rate);

        switch (ival.type) {
        case V4L2_FRMIVAL_TYPE_DISCRETE:
            g_debug("%u/%u",
                    ival.discrete.numerator,
                    ival.discrete.denominator);

            fmt_frm_rate->frame_rates.min = ival.discrete.denominator;

            break;

        case V4L2_FRMIVAL_TYPE_CONTINUOUS:
            g_debug("%u/%u - %u/%u",
                    ival.stepwise.min.numerator,
                    ival.stepwise.min.denominator,
                    ival.stepwise.max.numerator,
                    ival.stepwise.max.denominator);

            fmt_frm_rate->frame_rates.min = ival.stepwise.min.denominator;
            fmt_frm_rate->frame_rates.max = ival.stepwise.max.denominator;
            fmt_frm_rate->frame_rates.step = 1;

            goto out;

        case V4L2_FRMIVAL_TYPE_STEPWISE:
            g_debug("%u/%u - %u/%u (by %u/%u)",
                    ival.stepwise.min.numerator,
                    ival.stepwise.min.denominator,
                    ival.stepwise.max.numerator,
                    ival.stepwise.max.denominator,
                    ival.stepwise.step.numerator,
                    ival.stepwise.step.denominator);

            fmt_frm_rate->frame_rates.min = ival.stepwise.min.denominator;
            fmt_frm_rate->frame_rates.max = ival.stepwise.max.denominator;
            fmt_frm_rate->frame_rates.step = ival.stepwise.step.denominator;

            goto out;

        default:
            break;
        }
    }

out:
    if (ret == 0) {
        g_print("\n%s: Enumerated %d frame intervals\n", __func__
                , g_list_length(vid_fmt_frm_rate_l));
        g_return_val_if_fail(i == g_list_length(vid_fmt_frm_rate_l), -EINVAL);
        *p_vid_fmt_frm_rate_l = vid_fmt_frm_rate_l;
    }

    return ret;
}

static void video_frame_size_discrete(struct v4l2_device *dev,
                                      struct video_format_frame *vid_frame,
                                      struct v4l2_frmsizeenum *frame)
{
    g_debug("\tFrame size (D): %ux%u (", frame->discrete.width,
            frame->discrete.height);

    vid_frame->frame.width.min = htole32(frame->discrete.width);
    vid_frame->frame.width.max = htole32(frame->discrete.width);
    vid_frame->frame.height.min = htole32(frame->discrete.height);
    vid_frame->frame.height.max = htole32(frame->discrete.height);

    if (video_enum_frame_intervals(dev, frame->pixel_format,
                                   frame->discrete.width,
                                   frame->discrete.height,
                                   &vid_frame->frm_rate_l) < 0) {
        g_printerr("%s: video_enum_frame_intervals failed!", __func__);
    }
    g_debug(")");
}

static void video_frame_size_continuous(struct v4l2_device *dev,
                                        struct video_format_frame *vid_frame,
                                        struct v4l2_frmsizeenum *frame)
{
    g_debug("\tFrame size (C): %ux%u - %ux%u (",
            frame->stepwise.min_width,
            frame->stepwise.min_height,
            frame->stepwise.max_width,
            frame->stepwise.max_height);

    vid_frame->frame.width.min = htole32(frame->stepwise.min_width);
    vid_frame->frame.width.max = htole32(frame->stepwise.max_width);
    vid_frame->frame.width.step = htole32(frame->stepwise.step_width);
    vid_frame->frame.height.min = htole32(frame->stepwise.min_height);
    vid_frame->frame.height.max = htole32(frame->stepwise.max_height);
    vid_frame->frame.height.step = htole32(frame->stepwise.step_height);

    /* driver sanity check */
    if (frame->stepwise.step_height != 1 ||
        frame->stepwise.step_width != 1) {
        g_printerr("Warning: invalid step for continuous framesize");
    }

    if (video_enum_frame_intervals(dev, frame->pixel_format,
                                   frame->stepwise.max_width,
                                   frame->stepwise.max_height,
                                   &vid_frame->frm_rate_l) < 0) {
        g_printerr("%s: video_enum_frame_intervals failed!\n", __func__);
    }

    g_debug(")");
}

static void video_frame_size_stepwise(struct v4l2_device *dev,
                                      struct video_format_frame *vid_frame,
                                      struct v4l2_frmsizeenum *frame)
{
    g_debug("\tFrame size (S): %ux%u - %ux%u (by %ux%u) (",
            frame->stepwise.min_width,
            frame->stepwise.min_height,
            frame->stepwise.max_width,
            frame->stepwise.max_height,
            frame->stepwise.step_width,
            frame->stepwise.step_height);

    vid_frame->frame.width.min = htole32(frame->stepwise.min_width);
    vid_frame->frame.width.max = htole32(frame->stepwise.max_width);
    vid_frame->frame.width.step = htole32(frame->stepwise.step_width);
    vid_frame->frame.height.min = htole32(frame->stepwise.min_height);
    vid_frame->frame.height.max = htole32(frame->stepwise.max_height);
    vid_frame->frame.height.step = htole32(frame->stepwise.step_height);

    if (video_enum_frame_intervals(dev, frame->pixel_format,
                                frame->stepwise.max_width,
                                frame->stepwise.max_height,
                                &vid_frame->frm_rate_l) < 0) {
        g_printerr("%s: video_enum_frame_intervals failed!\n", __func__);
    }

    g_debug(")");
}

static int video_enum_frame_sizes(struct v4l2_device *dev,
                                  __u32 pixelformat, GList **p_vid_fmt_frm_l)
{
    struct v4l2_frmsizeenum frame;
    struct video_format_frame *vid_frame = NULL;
    GList *vid_fmt_frm_l = NULL;
    unsigned int i;
    int ret;

    if (!dev) {
        return -EINVAL;
    }

    for (i = 0; ; ++i) {
        memset(&frame, 0, sizeof frame);
        frame.index = i;
        frame.pixel_format = pixelformat;
        ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMESIZES, &frame);
        if (ret < 0) {
            if (errno == EINVAL) /* EINVAL means no more frame sizes */
                ret = 0;
            else
                g_printerr("VIDIOC_ENUM_FRAMESIZES failed: %s (%d)\n",
                           g_strerror(errno), errno);
            break;
        }

        /* driver sanity checks */
        if (i != frame.index)
            g_printerr("Warning: driver returned wrong frame index "
                       "%u.\n", frame.index);
        if (pixelformat != frame.pixel_format)
            g_printerr("Warning: driver returned wrong frame pixel "
                       "format %08x.\n", frame.pixel_format);

        /* allocate video_format_frame */
        vid_frame = g_new0(struct video_format_frame, 1);
        /* keep a copy of v4l2 frmsizeenum struct */
        memcpy(&vid_frame->v4l_framesize, &frame,
               sizeof(struct v4l2_frmsizeenum));
        vid_fmt_frm_l = g_list_append(vid_fmt_frm_l, vid_frame);

        switch (frame.type) {
        case V4L2_FRMSIZE_TYPE_DISCRETE:
            video_frame_size_discrete(dev, vid_frame, &frame);
            break;
        case V4L2_FRMSIZE_TYPE_CONTINUOUS:
            video_frame_size_continuous(dev, vid_frame, &frame);
            break;
        case V4L2_FRMSIZE_TYPE_STEPWISE:
            video_frame_size_stepwise(dev, vid_frame, &frame);
            break;
        default:
            break;
        }
    }
    if (ret == 0) {
        g_print("%s: Enumerated %d frame sizes and %d frame intervals\n",
                __func__, g_list_length(vid_fmt_frm_l),
                g_list_length(vid_frame->frm_rate_l));

        vid_frame->frame.num_rates =
            htole32(g_list_length(vid_frame->frm_rate_l));

        g_return_val_if_fail(i == g_list_length(vid_fmt_frm_l), -EINVAL);
        *p_vid_fmt_frm_l = vid_fmt_frm_l;
    }

    return ret;
}

int video_send_decoder_start_cmd(struct v4l2_device *dev)
{
    return v4l2_issue_cmd(dev->fd,  /*cmd=*/V4L2_DEC_CMD_START, /*flags=*/0);
}

static int video_querycap(struct v4l2_device *dev)
{
    struct v4l2_capability cap;
    unsigned int caps;
    bool has_video;
    bool has_meta;
    bool has_capture;
    bool has_output;
    bool has_mplane;
    int ret;

    memset(&cap, 0, sizeof cap);
    ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        return 0;
    }

    caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ?
            cap.device_caps :
            cap.capabilities;

    has_video = caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE |
                        V4L2_CAP_VIDEO_CAPTURE |
                        V4L2_CAP_VIDEO_OUTPUT_MPLANE |
                        V4L2_CAP_VIDEO_OUTPUT);
    has_meta = caps & (V4L2_CAP_META_CAPTURE |
                       V4L2_CAP_META_OUTPUT);
    has_capture = caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE |
                          V4L2_CAP_VIDEO_CAPTURE |
                          V4L2_CAP_META_CAPTURE);
    has_output = caps & (V4L2_CAP_VIDEO_OUTPUT_MPLANE |
                         V4L2_CAP_VIDEO_OUTPUT |
                         V4L2_CAP_META_OUTPUT);
    has_mplane = caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE |
                         V4L2_CAP_VIDEO_OUTPUT_MPLANE |
                         V4L2_CAP_VIDEO_M2M_MPLANE);

    g_print("Device '%s' on '%s' (driver '%s') "
            "supports%s%s%s%s %s mplanes.\n",
            cap.card, cap.bus_info, cap.driver,
            has_video ? " video," : "",
            has_meta ? " meta-data," : "",
            has_capture ? " capture," : "",
            has_output ? " output," : "",
            has_mplane ? "with" : "without");

    dev->capabilities = caps;
    dev->has_mplane = has_mplane;

    return 0;
}

void v4l2_set_device_type(struct v4l2_device *dev, enum v4l2_buf_type type,
                          struct v4l2_fmtdesc *fmt_desc)
{
    if (fmt_desc->flags & V4L2_FMT_FLAG_COMPRESSED) {
        switch (fmt_desc->pixelformat) {
        case V4L2_PIX_FMT_H263:
        case V4L2_PIX_FMT_H264:
        case V4L2_PIX_FMT_H264_NO_SC:
        case V4L2_PIX_FMT_H264_MVC:
        case V4L2_PIX_FMT_MPEG1:
        case V4L2_PIX_FMT_MPEG2:
        case V4L2_PIX_FMT_MPEG4:
        case V4L2_PIX_FMT_XVID:
        case V4L2_PIX_FMT_VC1_ANNEX_G:
        case V4L2_PIX_FMT_VC1_ANNEX_L:
        case V4L2_PIX_FMT_VP8:
        case V4L2_PIX_FMT_VP9:
        case V4L2_PIX_FMT_HEVC:
        case V4L2_PIX_FMT_FWHT:
            if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
                dev->dev_type |= STATEFUL_DECODER;
            }
            if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                dev->dev_type |= STATEFUL_DECODER;
            }
            if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                dev->dev_type |= STATEFUL_ENCODER;
            }
            if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
                dev->dev_type |= STATEFUL_ENCODER;
            }
            break;
        case V4L2_PIX_FMT_MPEG2_SLICE:
        case V4L2_PIX_FMT_FWHT_STATELESS:
            if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
                dev->dev_type |= STATELESS_DECODER;
            }
            if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                dev->dev_type |= STATELESS_DECODER;
            }
            if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                dev->dev_type |= STATELESS_ENCODER;
            }
            if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
                dev->dev_type |= STATELESS_ENCODER;
            }
            break;
        default:
            break;
        }
    }
}

enum v4l2_buf_type
get_v4l2_buf_type(enum virtio_video_queue_type queue_type, bool has_mplane)
{
    enum v4l2_buf_type buf_type = 0;

    switch (queue_type) {
    case VIRTIO_VIDEO_QUEUE_TYPE_INPUT:
        buf_type = has_mplane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
            : V4L2_BUF_TYPE_VIDEO_OUTPUT;
        break;
    case VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT:
        buf_type = has_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
            : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        break;
    default:
        g_warning("%s: Unknown queue_type!", __func__);
        break;
    }

    g_debug("%s: queue_type(0x%x) has_mplane(%d), buf_type(%s)",
            __func__, queue_type, has_mplane, v4l2_buf_type_name(buf_type));

    return buf_type;
}

enum v4l2_memory get_v4l2_memory(enum virtio_video_mem_type mem_type)
{
    /* if using GUEST_PAGES queued using USERPTR mechanism */
    enum v4l2_memory memory = V4L2_MEMORY_USERPTR;
    if (mem_type == VIRTIO_VIDEO_MEM_TYPE_VIRTIO_OBJECT) {
        memory = V4L2_MEMORY_DMABUF;
    } else {
        assert(mem_type == VIRTIO_VIDEO_MEM_TYPE_GUEST_PAGES);
    }
    g_debug("%s: mem_type(0x%x) memory(0x%x)", __func__, mem_type, memory);
    return memory;
}

enum virtio_video_mem_type
get_queue_mem_type(struct stream *s,
                   enum virtio_video_queue_type queue_type)
{
    if (queue_type == VIRTIO_VIDEO_QUEUE_TYPE_INPUT) {
        return s->vio_stream.in_mem_type;
    } else {
        return s->vio_stream.out_mem_type;
    }
}

int video_free_buffers(int fd, enum v4l2_buf_type type, enum v4l2_memory memory)
{
    int ret, count = 0;
    g_debug("%s: v4l2_buf_type: %s: Issuing REQBUFS 0"
            , __func__, v4l2_buf_type_name(type));

    /*
     * Applications can call ioctl VIDIOC_REQBUFS again to change the number
     * of buffers. Note that if any buffers are still mapped or exported via
     * DMABUF, then ioctl VIDIOC_REQBUFS can only succeed if the
     * V4L2_BUF_CAP_SUPPORTS_ORPHANED_BUFS capability is set. Otherwise ioctl
     * VIDIOC_REQBUFS will return the EBUSY error code. If
     * V4L2_BUF_CAP_SUPPORTS_ORPHANED_BUFS is set, then these buffers are
     * orphanedand will be freed when they are unmapped or when the exported
     * DMABUF fds are closed. A count value of zero frees or orphans all
     * buffers, after aborting or finishing any DMA in progress, an implicit
     * VIDIOC_STREAMOFF.
     */

    /* TODO support V4L2_BUF_CAP_SUPPORTS_ORPHANED_BUFS */
    ret = v4l2_ioctl_reqbuf(fd, type, memory, &count);
    return ret;
}

int video_resource_create(struct stream *s,
                          uint32_t queue_type, uint32_t queue_len)
{
    int ret, count;
    enum v4l2_buf_type buf_type = get_v4l2_buf_type(queue_type, s->has_mplane);
    enum virtio_video_mem_type mem_type = get_queue_mem_type(s, queue_type);

    count = queue_len;
    ret = v4l2_ioctl_reqbuf(s->fd, buf_type, get_v4l2_memory(mem_type), &count);
    if (ret < 0) {
        return ret;
    }

    if (V4L2_TYPE_IS_OUTPUT(buf_type)) {
        s->output_bufcount = count;
    } else if (V4L2_TYPE_IS_CAPTURE(buf_type)) {
        s->capture_bufcount = count;
    }

    if (count > queue_len) {
        g_critical("Unsupported feature: driver initiated more buffers(%d) "
                   "than requested(%d)", count, queue_len);
        return -EINVAL;
    }

    return ret;
}

int v4l2_streamon(struct stream *s, enum v4l2_buf_type type)
{
    int ret = 0;
    ret = ioctl(s->fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        g_printerr("VIDIOC_STREAMON failed: fd=(%d) buf type=%s: %s (%d)\n",
                   s->fd, v4l2_buf_type_name(type), g_strerror(errno), errno);
    } else {
        g_debug("%s: VIDIOC_STREAMON OK fd=(%d) buf type: %s",
                __func__, s->fd, v4l2_buf_type_name(type));
        if (V4L2_TYPE_IS_OUTPUT(type)) {
            s->output_streaming = true;
        }
        if (V4L2_TYPE_IS_CAPTURE(type)) {
            s->capture_streaming = true;
        }
    }
    return ret;
}

int v4l2_streamoff(struct stream *s, enum v4l2_buf_type type)
{
    int ret = 0;
    ret = ioctl(s->fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        g_printerr("VIDIOC_STREAMOFF failed: fd=(%d) buf type=%s: %s (%d)\n",
                   s->fd, v4l2_buf_type_name(type), g_strerror(errno), errno);
    } else {
        g_debug("%s: VIDIOC_STREAMOFF OK buf type: %s",
                __func__, v4l2_buf_type_name(type));

        if (V4L2_TYPE_IS_OUTPUT(type)) {
            s->output_streaming = false;
        }
        if (V4L2_TYPE_IS_CAPTURE(type)) {
            s->capture_streaming = false;
        }

        /*
         * if either queue has STREAMOFF applied, then we enter STOPPED
         * Assumes that s->mutex is held by calling function
         */
        s->stream_state = STREAM_STOPPED;
        g_cond_signal(&s->stream_cond);
    }
    return ret;
}

/* activate streaming on both queues */
int video_streamon(struct stream *s,
                   struct v4l2_device *dev, enum v4l2_buf_type type)
{
    int ret = 0;
    uint32_t id = 0;

    if (!s->subscribed_events) {
        /* subscribe for SOURCE_CHANGE event */
        if (dev->sup_dyn_res_switching) {
            ret = v4l2_ioctl_subscribe_event(
                s->fd, V4L2_EVENT_SOURCE_CHANGE, id);
            if (ret < 0) {
                g_printerr("V4L2_EVENT_SOURCE_CHANGE failed: %s (%d)\n",
                           g_strerror(errno), errno);
            }
        }
        /* subscribe for EOS event */
        ret = v4l2_ioctl_subscribe_event(s->fd, V4L2_EVENT_EOS, id);
        if (ret < 0) {
            g_printerr("V4L2_EVENT_EOS failed: %s (%d)\n",
                       g_strerror(errno), errno);
        }
        s->subscribed_events = true;
    }

    switch (type) {
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_META_OUTPUT:
        if (s->output_streaming == false) {
            ret |= v4l2_streamon(s, type);
        }
        break;

    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_META_CAPTURE:
        if (s->capture_streaming == false) {
            ret |= v4l2_streamon(s, type);
        }
        break;

    default:
        g_printerr("%s: unknown v4l2 buffer type!", __func__);
        ret = EINVAL;
    }

    if (s->stream_state != STREAM_DRAINING) {
        s->stream_state = STREAM_STREAMING;
        g_cond_signal(&s->stream_cond);
    }

    return ret;
}

int video_streamoff(struct stream *s, enum v4l2_buf_type type)
{
    int ret = 0;
    bool is_mplane = V4L2_TYPE_IS_MULTIPLANAR(type);
    enum v4l2_buf_type type2;

    switch (type) {
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_META_OUTPUT:
        if (s->output_streaming == true) {
            ret |= v4l2_streamoff(s, type);
        }

        if (s->capture_streaming == true) {
            type2 = is_mplane ?
                V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
                V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ret |= v4l2_streamoff(s, type2);
        }
        break;

    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_META_CAPTURE:
        if (s->capture_streaming == true) {
            ret |= v4l2_streamoff(s, type);
        }
        if (s->output_streaming == true) {
            type2 = is_mplane ?
                V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                V4L2_BUF_TYPE_VIDEO_OUTPUT;
            ret |= v4l2_streamoff(s, type2);
        }
        break;

    default:
        g_printerr("%s: unknown v4l2 buffer type!", __func__);
        ret = EINVAL;
    }

    return ret;
}

int v4l2_dmabuf_lookup_mplane(struct vuvbm_device *dev,
                              struct resource *res,
                              unsigned int iov_cnt)
{
    struct VuVideoDMABuf *buf = vuvbm_lookup(dev, res->uuid);
    if (!buf) {
        g_debug("Buffer not found. Creating.");
        res->buf = g_malloc0(sizeof(struct VuVideoDMABuf) * iov_cnt);
        for (int i = 0; i < iov_cnt; i++) {
            if (!vuvbm_buffer_create(dev, &res->buf[i], res->iov[i].iov_len)) {
                return -1;
            }
        }
        g_debug("Inserting buffer into the table.");
        g_hash_table_insert(dev->resource_uuids, res->buf, &res->uuid);
    } else {
        res->buf = buf;
        g_debug("Buffer found.");
    }

    return 0;
}

int v4l2_dmabuf_lookup(struct vuvbm_device *dev,
                       struct resource *res,
                       unsigned int iov_len)
{
    struct VuVideoDMABuf *buf = vuvbm_lookup(dev, res->uuid);
    if (!buf) {
        g_debug("Buffer not found. Creating.");
        res->buf = g_new0(struct VuVideoDMABuf, 1);
        if (!vuvbm_buffer_create(dev, res->buf, iov_len)) {
            return -1;
        }
        g_debug("Inserting buffer into the table.");
        g_hash_table_insert(dev->resource_uuids, res->buf, &res->uuid);
    } else {
        res->buf = buf;
        g_debug("Buffer found.");
    }

    return 0;
}

int v4l2_queue_buffer(enum v4l2_buf_type type,
                      enum v4l2_memory memory,
                      struct virtio_video_resource_queue *qcmd,
                      struct resource *res, struct stream *s,
                      struct v4l2_device *dev, struct vuvbm_device *bm_dev)
{
    struct v4l2_buffer vbuf;
    int ret = 0;
    int fd = s->fd;

    memset(&vbuf, 0, sizeof(vbuf));
    vbuf.index = res->v4l2_index;

    vbuf.type = type;
    vbuf.memory = memory;
    vbuf.field = V4L2_FIELD_NONE;
    vbuf.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

    g_debug("%s: type=%s memory=%d index=%d", __func__,
            v4l2_buf_type_name(type), memory, vbuf.index);

    convert_to_timeval(le64toh(qcmd->timestamp), &vbuf.timestamp);

    if (memory == V4L2_MEMORY_USERPTR) {
        if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
            /* for mplane length field is number of elements in planes array */
            vbuf.length = res->vio_resource.num_planes;
            vbuf.m.planes = g_malloc0(sizeof(struct v4l2_plane)
                                    * res->vio_resource.num_planes);

            for (int i = 0; i < vbuf.length; i++) {
                vbuf.m.planes[i].m.userptr =
                    (unsigned long)res->iov[i].iov_base;
                vbuf.m.planes[i].length = (unsigned long)res->iov[i].iov_len;
            }
        } else {
            assert(res->iov != NULL);
            vbuf.m.userptr = (unsigned long)res->iov[0].iov_base;
            vbuf.length = res->iov[0].iov_len;
            g_debug("%s: iov_base = 0x%p", __func__, res->iov[0].iov_base);
            g_debug("%s: iov_len = 0x%lx", __func__, res->iov[0].iov_len);
        }
    } else {
        assert(memory == V4L2_MEMORY_DMABUF);
        if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
            vbuf.length = res->vio_resource.num_planes;
            vbuf.m.planes = g_malloc0(sizeof(struct v4l2_plane) * vbuf.length);
            if (!res->buf) {
                ret = v4l2_dmabuf_lookup_mplane(bm_dev, res, vbuf.length);
                if (ret < 0) {
                    g_warning("Buffer create failed.");
                    return ret;
                }
            }

            for (int i = 0; i < vbuf.length; i++) {
                vbuf.m.planes[i].m.fd = res->buf->dev->get_fd(&res->buf[i]);
                vbuf.m.planes[i].length = (unsigned long)res->iov[i].iov_len;
                /* Copy virtio shared memory contents to DMA buffer */
                memcpy(res->buf[i].start,
                       res->iov[i].iov_base, res->iov[i].iov_len);
            }
        } else {
            if (!res->buf) {
                ret = ioctl(fd, VIDIOC_QUERYBUF, &vbuf);
                if (ret == -EINVAL) {
                    g_printerr("VIDIOC_QUERYBUF failed: %s (%d)\n",
                               g_strerror(errno), errno);
                    return ret;
                }

                ret = v4l2_dmabuf_lookup(bm_dev, res, vbuf.length);
                if (ret < 0) {
                    g_warning("Buffer create failed.");
                    return ret;
                }
            }
            vbuf.m.fd = res->buf->dev->get_fd(res->buf);
            /* Copy virtio shared memory contents to DMA buffer */
            memcpy(res->buf->start, res->iov[0].iov_base, res->iov[0].iov_len);
        }
    }

    if (V4L2_TYPE_IS_OUTPUT(type)) {
        if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
            for (int i = 0; i < vbuf.length; i++) {
                vbuf.m.planes[i].bytesused = vbuf.m.planes[i].length;
            }
        } else {
            vbuf.bytesused = vbuf.length;
        }
    }

    ret = ioctl(fd, VIDIOC_QBUF, &vbuf);
    if (ret < 0) {
        qcmd->hdr.type = VIRTIO_VIDEO_RESP_ERR_INVALID_PARAMETER;
        g_printerr("Unable to QBUF: %s (%d).\n", g_strerror(errno), errno);
        return ret;
    }

    ret = video_streamon(s, dev, type);
    if (ret < 0) {
        g_printerr("video_streamon failed (%d)", ret);
        /* only print error, as video_streamon() does both queues */
    }

    res->queued = true;
    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        g_free(vbuf.m.planes);
    }

    g_debug("%s: Queued resource-id(%d) buf_type=%s v4l2_index(%d) "
            "virtio_queue(0x%x)", __func__, res->vio_resource.resource_id,
            v4l2_buf_type_name(type), res->v4l2_index,
            res->vio_resource.queue_type);

    return ret;

}

int v4l2_dequeue_buffer(int fd, enum v4l2_buf_type type,
                        enum v4l2_memory memory,
                        struct stream *s)
{
    struct v4l2_buffer vbuf;
    int ret = 0;
    struct resource *r;
    struct virtio_video_resource_queue_resp resp;
    struct vu_video_ctrl_command *vio_cmd;

    memset(&vbuf, 0, sizeof(vbuf));

    vbuf.type = type;
    vbuf.memory = memory;

    vbuf.field = V4L2_FIELD_NONE;
    vbuf.flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        /* for mplane length field is number of elements in planes array */
        vbuf.length = VIRTIO_VIDEO_MAX_PLANES;
        vbuf.m.planes = g_malloc0(sizeof(struct v4l2_plane)
                                  * VIRTIO_VIDEO_MAX_PLANES);

        g_debug("%s: mplane allocating planes array", __func__);
    }

    ret = ioctl(fd, VIDIOC_DQBUF, &vbuf);
    if (ret < 0) {
        g_printerr("Unable to DQBUF: %s (%d).\n", g_strerror(errno), errno);
        return -errno;
    }

    g_debug("%s: VIDIOC_DQBUF OK index(%d)", __func__, vbuf.index);

    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        g_free(vbuf.m.planes);
    }

    r = find_resource_by_v4l2index(s, type, vbuf.index);
    if (!r) {
        g_printerr("%s: Can't find resource for dequeued buffer!", __func__);
        return -EINVAL;
    }

    r->queued = false;
    vio_cmd = r->vio_q_cmd;

    resp.flags = 0x0;
    resp.hdr.stream_id = r->stream_id;
    resp.hdr.type = VIRTIO_VIDEO_RESP_OK_NODATA;
    resp.timestamp = htole64(r->vio_res_q.timestamp);

    /* encoder only */
    resp.size = htole32(vbuf.bytesused);

    if (vbuf.flags & V4L2_BUF_FLAG_LAST &&
        s->stream_state == STREAM_DRAINING) {
        resp.flags |= VIRTIO_VIDEO_BUFFER_FLAG_EOS;
        s->stream_state = STREAM_STOPPED;
        g_cond_signal(&s->stream_cond);
    }

    if (vbuf.flags & V4L2_BUF_FLAG_KEYFRAME) {
        resp.flags |= VIRTIO_VIDEO_BUFFER_FLAG_IFRAME;
    }
    if (vbuf.flags & V4L2_BUF_FLAG_PFRAME) {
        resp.flags |= VIRTIO_VIDEO_BUFFER_FLAG_PFRAME;
    }
    if (vbuf.flags & V4L2_BUF_FLAG_BFRAME) {
        resp.flags |= VIRTIO_VIDEO_BUFFER_FLAG_PFRAME;
    }

    if (vbuf.flags & V4L2_BUF_FLAG_ERROR) {
        resp.flags |= VIRTIO_VIDEO_BUFFER_FLAG_ERR;
        g_critical("%s: V4L2_BUF_FLAG_ERROR\n", __func__);
    }

    g_debug("%s: Send queue_buffer reply: stream_id=%d type=0x%x "
            "flags=0x%x resource_id=%d t=%llx", __func__,
            resp.hdr.stream_id, resp.hdr.type, resp.flags,
            r->vio_resource.resource_id, resp.timestamp);

    send_ctrl_response(vio_cmd, (uint8_t *) &resp,
                       sizeof(struct virtio_video_resource_queue_resp));

    vio_cmd->finished = true;
    free_resource_mem(r);

    return ret;
}

int v4l2_video_get_selection(int fd, enum v4l2_buf_type type,
                             struct v4l2_selection *sel)
{
    int ret = 0;

    if (!sel) {
        return -EINVAL;
    }

    memset(sel, 0, sizeof(struct v4l2_selection));

    sel->type = type;

    if (V4L2_TYPE_IS_CAPTURE(type)) {
        sel->target = V4L2_SEL_TGT_COMPOSE;
    } else if (V4L2_TYPE_IS_OUTPUT(type)) {
        sel->target = V4L2_SEL_TGT_CROP;
    }

    ret = ioctl(fd, VIDIOC_G_SELECTION, sel);
    if (ret < 0) {
        g_printerr("Unable to get selection: %s (%d)\n",
                   g_strerror(errno), errno);
        return ret;
    }

    g_debug("%s: VIDIOC_G_SELECTION: fd=(%d) %s: left=(%d) "
            "top=(%d) width=(%d) height=(%d)",
            __func__, fd, v4l2_buf_type_name(type), sel->r.left,
            sel->r.top, sel->r.width, sel->r.height);

    return ret;
}

int v4l2_video_set_selection(int fd, enum v4l2_buf_type type,
                             struct v4l2_selection *sel)
{
    int ret = 0;

    if (!sel) {
        return -EINVAL;
    }

    sel->type = type;
    sel->target = V4L2_SEL_TGT_COMPOSE;
    /* flags 0 - the driver can adjust the rect size freely */
    sel->flags = 0;

    ret = ioctl(fd, VIDIOC_S_SELECTION, sel);
    if (ret < 0) {
        g_printerr("Unable to set selection: fd=(%d) left=(%d) top=(%d) "
                   "width=(%d) height=(%d): %s (%d)\n",
                   fd, sel->r.left, sel->r.top, sel->r.width, sel->r.height,
                   g_strerror(errno), errno);
        return ret;
    }

    g_debug("%s: VIDIOC_S_SELECTION: fd=(%d) left=(%d) "
            "top=(%d) width=(%d) height=(%d)",
            __func__, fd, sel->r.left, sel->r.top, sel->r.width, sel->r.height);

    return ret;
}

int v4l2_video_get_param(int fd, enum v4l2_buf_type type,
                          struct v4l2_streamparm *sparam)
{
    int ret = 0;

    if (!sparam) {
        return -EINVAL;
    }

    memset(sparam, 0, sizeof(struct v4l2_streamparm));
    sparam->type = type;

    ret = ioctl(fd, VIDIOC_G_PARM, sparam);
    if (ret < 0) {
        g_printerr("Unable to VIDIOC_G_PARAM: %s (%d)\n",
                   g_strerror(errno), errno);
        return ret;
    }

    g_debug("%s: VIDIOC_G_PARM timeperframe (%d/%d)", __func__,
            sparam->parm.capture.timeperframe.numerator,
            sparam->parm.capture.timeperframe.denominator);

    return ret;
}

int v4l2_video_get_format(int fd, enum v4l2_buf_type type,
                          struct v4l2_format *fmt)
{
    unsigned int i;
    int ret;

    if (!fmt) {
        return -EINVAL;
    }

    memset(fmt, 0, sizeof(struct v4l2_format));
    fmt->type = type;

    ret = ioctl(fd, VIDIOC_G_FMT, fmt);
    if (ret < 0) {
        g_printerr("Unable to get format: %s (%d)\n",
                   g_strerror(errno), errno);
        return ret;
    }

    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        g_print("Video format: %s (%08x) %ux%u field %s, %u planes:\n",
                v4l2_format_name(fmt->fmt.pix_mp.pixelformat),
                                 fmt->fmt.pix_mp.pixelformat,
                                 fmt->fmt.pix_mp.width,
                                 fmt->fmt.pix_mp.height,
                v4l2_field_name(fmt->fmt.pix_mp.field),
                                fmt->fmt.pix_mp.num_planes);

        for (i = 0; i < fmt->fmt.pix_mp.num_planes; i++) {
            g_print(" * Stride %u, buffer size %u\n",
                   fmt->fmt.pix_mp.plane_fmt[i].bytesperline,
                   fmt->fmt.pix_mp.plane_fmt[i].sizeimage);
        }
    } else if (V4L2_TYPE_IS_META(type)) {
        g_print("Meta-data format: %s (%08x) buffer size %u\n",
                v4l2_format_name(fmt->fmt.meta.dataformat),
                                 fmt->fmt.meta.dataformat,
                                 fmt->fmt.meta.buffersize);
    } else {
        g_print("Video format: %s (%08x) %ux%u (stride %u) field %s "
                "buffer size %u\n",
                v4l2_format_name(fmt->fmt.pix.pixelformat),
                                 fmt->fmt.pix.pixelformat,
                                 fmt->fmt.pix.width, fmt->fmt.pix.height,
                                 fmt->fmt.pix.bytesperline,
                v4l2_field_name(fmt->fmt.pix_mp.field),
                                fmt->fmt.pix.sizeimage);
    }

    return 0;
}

int v4l2_video_set_format(int fd, enum v4l2_buf_type type,
                          struct virtio_video_params *p)
{
    struct v4l2_format fmt;
    int ret = 0;
    unsigned int i;
    uint32_t pixfmt;

    if (!p) {
        return -EINVAL;
    }

    memset(&fmt, 0, sizeof fmt);
    fmt.type = type;
    pixfmt = virtio_video_format_to_v4l2(le32toh(p->format));

    if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
        fmt.fmt.pix_mp.width = le32toh(p->frame_width);
        fmt.fmt.pix_mp.height = le32toh(p->frame_height);
        fmt.fmt.pix_mp.pixelformat = pixfmt;
        /*
         * V4L2_FIELD_NONE - matches what Linux frontend driver does in
         * virtio_video_format_from_info()
         */
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        /*fmt.fmt.pix_mp.num_planes = info->n_planes;*/
        fmt.fmt.pix_mp.num_planes = le32toh(p->num_planes);
        fmt.fmt.pix_mp.flags = 0;

        for (i = 0; i < le32toh(p->num_planes); i++) {
            fmt.fmt.pix_mp.plane_fmt[i].bytesperline =
                le32toh(p->plane_formats[i].stride);
            fmt.fmt.pix_mp.plane_fmt[i].sizeimage =
                le32toh(p->plane_formats[i].plane_size);
        }
    } else if (V4L2_TYPE_IS_SINGLEPLANAR(type)) {
        fmt.fmt.pix.width = le32toh(p->frame_width);
        fmt.fmt.pix.height = le32toh(p->frame_height);
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        fmt.fmt.pix.bytesperline = le32toh(p->plane_formats[0].stride);
        fmt.fmt.pix.sizeimage = le32toh(p->plane_formats[0].plane_size);
        fmt.fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
        fmt.fmt.pix.flags = 0;
    }

    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        g_printerr("Unable to set format: %s (%d)\n",
                   g_strerror(errno), errno);
    }
    return ret;
}

int
v4l2_set_pixel_format(int fd, enum v4l2_buf_type buf_type, uint32_t pixelformat)
{
    int ret = 0;
    struct v4l2_format cur_fmt;

    g_debug("%s: buf_type=0x%x pixelformat=0x%x", __func__,
            buf_type, pixelformat);

    /* get the currently set format */
    ret = v4l2_video_get_format(fd, buf_type, &cur_fmt);
    if (ret < 0) {
        g_printerr("%s: v4l2_video_get_format() failed\n", __func__);
        return ret;
    }

    /* keep defaults and set correct pixel format */
    if (V4L2_TYPE_IS_MULTIPLANAR(cur_fmt.type)) {
        g_print("%s: Format is mplane\n", __func__);
        cur_fmt.fmt.pix_mp.pixelformat = pixelformat;
    } else if (V4L2_TYPE_IS_SINGLEPLANAR(cur_fmt.type)) {
        g_print("%s: Format is splane\n", __func__);
        cur_fmt.fmt.pix.pixelformat = pixelformat;
    }

    ret = ioctl(fd, VIDIOC_S_FMT, &cur_fmt);
    if (ret < 0) {
        g_printerr("Unable to set format: %s (%d)\n",
                   g_strerror(errno), errno);
    }

    return ret;
}

int video_enum_formats(struct v4l2_device *dev, enum v4l2_buf_type type,
                       GList **p_fmt_list, bool only_enum_fmt)
{
    struct v4l2_fmtdesc fmt;
    struct video_format *vid_fmt = NULL;
    GList *fmt_list = NULL;
    unsigned int index;
    int ret = 0;

    if (!dev) {
        return -EINVAL;
    }

    for (index = 0; ; ++index) {
        memset(&fmt, 0, sizeof fmt);
        fmt.index = index;
        fmt.type = type;

        ret = ioctl(dev->fd, VIDIOC_ENUM_FMT, &fmt);
        if (ret < 0) {
            if (errno == EINVAL) {
                ret = 0;
            } else {
                g_printerr("%s: VIDIOC_ENUM_FMT failed %s (%d)\n",
                           __func__, g_strerror(errno), errno);
            }
        break;
        }

        /* do some driver sanity checks */
        if (index != fmt.index) {
            g_warning("v4l2 driver modified index %u\n", fmt.index);
        }
        if (type != fmt.type) {
            g_warning("v4l2 driver modified type %u\n", fmt.type);
        }
        g_debug("\tFormat %u: %s (%08x)", index,
                v4l2_format_name(fmt.pixelformat), fmt.pixelformat);
        g_debug("\tType: %s (%u)", v4l2_buf_type_name(fmt.type),
                fmt.type);
        g_debug("\tName: %.32s", fmt.description);
        g_debug("\tFlags: 0x%x", fmt.flags);

        if (fmt.flags & V4L2_FMT_FLAG_DYN_RESOLUTION &&
            fmt.flags & V4L2_FMT_FLAG_COMPRESSED) {
            g_print("dynamic resolution switching supported\n");
            dev->sup_dyn_res_switching = true;
        }

        /* test if pixelformat converts to virtio */
        if (!virtio_video_v4l2_format_to_virtio(fmt.pixelformat)) {
            g_info("Skipping Format %s (%08x) - no virtio-video equivalent",
                   v4l2_format_name(fmt.pixelformat), fmt.pixelformat);
            continue;
        }

        vid_fmt = g_new0(struct video_format, 1);
        /* keep a copy of v4l2 struct */
        memcpy(&vid_fmt->fmt, &fmt, sizeof(struct v4l2_fmtdesc));
        /* add it to linked list */
        fmt_list = g_list_append(fmt_list, vid_fmt);

        if (!only_enum_fmt) {
            /* pass video_format to enum_frame_sizes */
            ret = video_enum_frame_sizes(dev, fmt.pixelformat,
                                         &vid_fmt->vid_fmt_frm_l);
            if (ret < 0) {
                g_printerr("video_enum_frame_sizes failed\n");
            }

            /* convert to virtio format */
            v4l2_to_virtio_fmtdesc(dev, vid_fmt, type);
        }

        /* determine type of v4l2 device */
        v4l2_set_device_type(dev, type, &fmt);
    }

    if (ret == 0) {
        g_print("%s: Enumerated %d formats on v4l2 %s queue",
                __func__, index, v4l2_buf_type_name(type));
        g_print(" %d formats are representable by virtio-video\n",
                g_list_length(fmt_list));
        if (!only_enum_fmt) {
            g_print("%s: Enumerated %d frame sizes\n",
                    __func__, g_list_length(vid_fmt->vid_fmt_frm_l));
        }

        *p_fmt_list = fmt_list;
    }

    return ret;
}

void video_free_frame_intervals(GList *frm_intervals_l)
{
    GList *l;
    struct video_format_frame_rates *vid_fmt_frm_rate;
    for (l = frm_intervals_l; l != NULL; l = l->next) {
        vid_fmt_frm_rate = l->data;
        g_free(vid_fmt_frm_rate);
    }
}

void video_free_frame_sizes(GList *frm_sz_l)
{
    GList *l;
    struct video_format_frame *vid_frame;
    for (l = frm_sz_l; l != NULL; l = l->next) {
        vid_frame = l->data;
        if (vid_frame->frm_rate_l) {
            video_free_frame_intervals(vid_frame->frm_rate_l);
        }
        g_free(vid_frame);
    }
}

void video_free_formats(GList **fmt_l)
{
    GList *l;
    struct video_format *vid_fmt;

    for (l = *fmt_l; l != NULL; l = l->next) {
        vid_fmt = l->data;
        if (vid_fmt->vid_fmt_frm_l) {
            video_free_frame_sizes(vid_fmt->vid_fmt_frm_l);
        }

        g_free(vid_fmt);
    }
}


static GByteArray *iterate_frame_rate_list(GByteArray *resp, GList *frm_rate_l)
{
    struct video_format_frame_rates *vid_fmt_frm_rate;

    /* iterate frame_rate list */
    for (; frm_rate_l != NULL; frm_rate_l = frm_rate_l->next) {
        vid_fmt_frm_rate = frm_rate_l->data;

        resp = g_byte_array_append(resp,
                                   (guint8 *) &vid_fmt_frm_rate->frame_rates,
                                   sizeof(struct virtio_video_format_range));
        }
    return resp;
}

static GByteArray *iterate_format_frame_list(GByteArray *resp, GList *fmt_frm_l)
{
    struct video_format_frame *vid_fmt_frm;
    GList *frm_rate_l = NULL;

    /* iterate format_frame list */
    for (; fmt_frm_l != NULL; fmt_frm_l = fmt_frm_l->next) {
        vid_fmt_frm = fmt_frm_l->data;

        if (!vid_fmt_frm->frm_rate_l) {
            vid_fmt_frm->frame.num_rates = htole32(0);
        } else {
            frm_rate_l = vid_fmt_frm->frm_rate_l;
            vid_fmt_frm->frame.num_rates = htole32(g_list_length(frm_rate_l));
        }

        g_debug("%s: num_rates(%d)", __func__,
                le32toh(vid_fmt_frm->frame.num_rates));

        resp = g_byte_array_append(resp,
                                   (guint8 *) &vid_fmt_frm->frame,
                                   sizeof(struct virtio_video_format_frame));

        if (frm_rate_l) {
            resp = iterate_frame_rate_list(resp, frm_rate_l);
        }
    }

    return resp;
}

static GByteArray *iterate_format_desc_list(GByteArray *resp, GList *fmt_desc_l)
{
    struct video_format *vid_fmt;
    GList *fmt_frm_l = NULL;

    for (; fmt_desc_l != NULL; fmt_desc_l = fmt_desc_l->next) {
        vid_fmt = fmt_desc_l->data;

        /* does video_format have a list of format_frame? */
        if (!vid_fmt->vid_fmt_frm_l) {
            vid_fmt->desc.num_frames = htole32(0);
        } else {
            fmt_frm_l = vid_fmt->vid_fmt_frm_l;
            vid_fmt->desc.num_frames = htole32(g_list_length(fmt_frm_l));
        }

        g_debug("%s: num_frames(%d)", __func__,
                le32toh(vid_fmt->desc.num_frames));

        resp = g_byte_array_append(resp,
                                   (guint8 *) &vid_fmt->desc,
                                   sizeof(struct virtio_video_format_desc));

        if (fmt_frm_l) {
            resp = iterate_format_frame_list(resp, fmt_frm_l);
        }
    }

    return resp;
}

GByteArray *create_query_cap_resp(struct virtio_video_query_capability *qcmd,
                           GList **fmt_l, GByteArray *resp)
{
    GList *fmt_desc_l;
    struct virtio_video_query_capability_resp cap_resp;

    fmt_desc_l = *fmt_l;

    cap_resp.hdr.type = VIRTIO_VIDEO_RESP_OK_QUERY_CAPABILITY;
    cap_resp.hdr.stream_id = qcmd->hdr.stream_id;
    cap_resp.num_descs = htole32(g_list_length(fmt_desc_l));

    assert(le32toh(cap_resp.num_descs) < MAX_FMT_DESCS);

    resp = g_byte_array_append(resp, (guint8 *) &cap_resp, sizeof(cap_resp));
    resp = iterate_format_desc_list(resp, fmt_desc_l);

    return resp;
}

/* timestamp in nsecs */
void convert_to_timeval(uint64_t timestamp, struct timeval *t)
{
    uint64_t f_nsecs;

    uint64_t nsecs;

    /* convert to seconds */
    t->tv_sec = timestamp / 1000000000;

    /* deal with fraction of a second */
    f_nsecs = t->tv_sec * 1000000000;
    t->tv_usec = (timestamp - f_nsecs) / 1000;

    /* sanity check above conversion */
    nsecs = t->tv_sec * 1000000000;
    nsecs += (t->tv_usec * 1000);

    if (timestamp != nsecs) {
        g_critical("%s: timestamp != nsecs", __func__);
    }
}

void v4l2_backend_free(struct v4l2_device *dev)
{
    if (dev && dev->opened) {
        close(dev->fd);
    }
    g_free(dev);
}

struct v4l2_device *v4l2_backend_init(const gchar *devname)
{
    struct v4l2_device *dev;
    int ret = 0;
    GList *vid_output_fmt_l = NULL;
    GList *vid_capture_fmt_l = NULL;
    enum v4l2_buf_type buf_type;

    if (!devname) {
        return NULL;
    }

    dev = g_malloc0(sizeof(struct v4l2_device));

    /* open the device */
    dev->fd = v4l2_open(devname);
    if (dev->fd < 0) {
        g_printerr("v4l2_open() failed!\n");
        goto err;
    }

    dev->opened = 1;
    dev->devname = devname;

    ret = video_querycap(dev);

    buf_type = dev->has_mplane ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
        : V4L2_BUF_TYPE_VIDEO_OUTPUT;

    /* enumerate coded formats on OUTPUT */
    ret = video_enum_formats(dev, buf_type,
                             &vid_output_fmt_l, true);
    if (ret < 0) {
        g_printerr("video_enum_formats() failed OUTPUT\n");
        goto err;
    }

    buf_type = dev->has_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
        : V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* enumerate coded formats on CAPTURE */
    ret = video_enum_formats(dev, buf_type,
                             &vid_capture_fmt_l, true);
    if (ret < 0) {
        g_printerr("video_enum_formats() failed CAPTURE\n");
        goto err2;
    }

    if (dev->dev_type & STATEFUL_ENCODER)
        g_print("%s: %s is a stateful encoder (0x%x)!\n", __func__,
                devname, dev->dev_type);

    if (dev->dev_type & STATEFUL_DECODER)
        g_print("%s: %s is a stateful decoder (0x%x)!\n", __func__,
                devname, dev->dev_type);

    video_free_formats(&vid_output_fmt_l);
    video_free_formats(&vid_capture_fmt_l);

    if (!(dev->dev_type & STATEFUL_ENCODER ||
          dev->dev_type & STATEFUL_DECODER)) {
        g_printerr("v4l2 device not supported! v4l2 backend only supports "
                   "stateful codec devices currently(%d)!\n", dev->dev_type);
        goto err3;
    }

    g_debug("%s: success!\n", __func__);
    return dev;

err3:
    video_free_formats(&vid_capture_fmt_l);
err2:
    video_free_formats(&vid_output_fmt_l);
err:
    v4l2_backend_free(dev);
    return NULL;
}

int v4l2_ioctl_query_control(int fd, uint32_t control, int32_t *value)
{
    int ret = 0;
    struct v4l2_queryctrl ctrl;

    g_debug("%s:%d", __func__, __LINE__);

    ctrl.id = control;

    ret = ioctl(fd, VIDIOC_QUERYCTRL, &ctrl);
    if (ret < 0) {
        g_printerr("Unable to query control: %s (%d)\n",
                   g_strerror(errno), errno);
        return ret;
    }

    *value = ctrl.type;
    g_debug("%s: ctrl=0x%x type=0x%x", __func__, control, *value);

    return ret;
}

int v4l2_ioctl_get_control(int fd , uint32_t control, int32_t *value)
{
    int ret = 0;
    struct v4l2_control ctrl;

    g_debug("%s:%d", __func__, __LINE__);

    ctrl.id = control;

    ret = ioctl(fd, VIDIOC_G_CTRL, &ctrl);
    if (ret < 0) {
        g_printerr("Unable to get control: %s (%d)\n",
                   g_strerror(errno), errno);
        return ret;
    }

    *value = ctrl.value;
    g_debug("%s: ctrl=0x%x value=0x%x", __func__, control, *value);

    return ret;
}

int v4l2_ioctl_reqbuf(int fd, enum v4l2_buf_type type,
                      enum v4l2_memory memory, int *count)
{
    int ret;
    struct v4l2_requestbuffers reqbuf;

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = type;
    reqbuf.memory = memory;
    reqbuf.count = *count;

    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret == -1) {
        if (errno == EBUSY) {
            g_critical("%s: EBUSY: buffers for %s still mapped or exported!\n",
                       __func__, v4l2_buf_type_name(type));
        } else {
            g_printerr("VIDIOC_REQBUFS failed: %s (%d)\n",
                       g_strerror(errno), errno);
        }
        return ret;
    }

    *count = reqbuf.count;

    g_debug("%s: VIDIOC_REQBUFS capabilities(0x%x) granted(%d)",
            __func__, reqbuf.capabilities, reqbuf.count);

    return ret;
}

int v4l2_ioctl_subscribe_event(int fd, uint32_t event_type, uint32_t id)
{
    int ret = 0;
    struct v4l2_event_subscription sub;

    memset(&sub, 0, sizeof(sub));
    sub.type = event_type;
    sub.id = 0;

    if (event_type == V4L2_EVENT_SOURCE_CHANGE) {
        sub.id = id;
    }

    ret = ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    if (ret < 0) {
        g_printerr("VIDIOC_SUBSCRIBE_EVENT failed: %s (%d)\n",
                   g_strerror(errno), errno);
        return ret;
    }

    g_debug("%s event(0x%x) OK", __func__, event_type);

    return ret;
}

int v4l2_issue_cmd(int fd,  uint32_t cmd, uint32_t flags)
{
    int ret = 0;
    struct v4l2_decoder_cmd decoder_cmd;
    memset(&decoder_cmd, 0, sizeof(struct v4l2_decoder_cmd));

    decoder_cmd.cmd = cmd;
    decoder_cmd.flags = flags;
    /* Normal speed */
    decoder_cmd.start.speed = 1000;

    ret = ioctl(fd, VIDIOC_DECODER_CMD, &decoder_cmd);
    if (ret < 0) {
        g_printerr("VIDIOC_DECODER_CMD(%d) failed fd=(%d): %s: (%d)\n",
                   cmd, fd, g_strerror(errno), errno);
        return ret;
    }

    g_debug("%s: VIDIOC_DECODER_CMD(%d) fd(%d) OK\n", __func__, cmd, fd);

    return ret;
}
