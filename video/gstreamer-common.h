/*
 * Copyright 2025 9elements GmbH
 *
 * Authors:
 *   David Milosevic <david.milosevic@9elements.com>
 *   Marcello Sylvester Bauer <marcello.bauer@9elements.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_VIDEO_GSTREAMER_COMMON_H
#define QEMU_VIDEO_GSTREAMER_COMMON_H

#define TYPE_VIDEODEV_GSTREAMER TYPE_VIDEODEV"-gstreamer"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

/*
 * GStreamer pipeline:
 *
 * <------------------- qemu-cmdline -------------------><--- qemu-runtime --->
 * [source] -> [converter #1] -> ... -> [converter #n] -> capsfilter -> appsink
 */
struct GStreamerVideodev {

    Videodev parent;

    GstElement *pipeline; // gstreamer pipeline
    GstElement *head;     // first element of pipeline (source)
    GstElement *tail;     // last element of cmdline pipeline
    GstElement *filter;   // dynamically generated capsfilter
    GstElement *sink;     // dynamnically generated appsink

    struct GStreamerVideoFrame {
        GstSample *sample;
        GstBuffer *buffer;
        GstMapInfo map_info;
    } current_frame;
};
typedef struct GStreamerVideodev GStreamerVideodev;

DECLARE_INSTANCE_CHECKER(GStreamerVideodev, GSTREAMER_VIDEODEV, TYPE_VIDEODEV_GSTREAMER)

void video_gstreamer_class_init(ObjectClass *oc, const void *data);
char *video_gstreamer_qemu_opt_get(QemuOpts *opts, const char *name);

#endif /* QEMU_VIDEO_GSTREAMER_COMMON_H */
