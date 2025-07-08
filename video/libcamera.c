/*
 * Copyright 2025 9elements GmbH
 *
 * Authors:
 *   David Milosevic <david.milosevic@9elements.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"
#include "video/gstreamer-common.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define TYPE_VIDEODEV_LIBCAMERA TYPE_VIDEODEV"-libcamera"

#define VIDEO_LIBCAMERA_PIPELINE_TEMPLATE \
    "libcamerasrc name=qemu_src camera-name=\"%s\" ! capsfilter caps=\"%s\" ! videoconvert name=qemu_vc ! capsfilter name=qemu_cf ! appsink name=qemu_sink"

struct LibcameraVideodev {

    GStreamerVideodev parent;
};
typedef struct LibcameraVideodev LibcameraVideodev;

DECLARE_INSTANCE_CHECKER(LibcameraVideodev, LIBCAMERA_VIDEODEV, TYPE_VIDEODEV_LIBCAMERA)

static char *video_libcamera_pipeline_string(const char *cam_name, const char *caps)
{
    char *pipeline_desc = NULL;
    size_t pipeline_template_len, pipeline_len;

    pipeline_template_len = strlen(VIDEO_LIBCAMERA_PIPELINE_TEMPLATE) - 4; // minus '%s' (x2)
    pipeline_len = strlen(cam_name) + strlen(caps) + pipeline_template_len + 1; // plus '\0'

    pipeline_desc = g_malloc(pipeline_len * sizeof(char));
    if (!pipeline_desc) {
        return NULL;
    }

    sprintf(pipeline_desc, VIDEO_LIBCAMERA_PIPELINE_TEMPLATE, cam_name, caps);
    return pipeline_desc;
}

static int video_libcamera_open(Videodev *vd, QemuOpts *opts, Error **errp)
{
    LibcameraVideodev *lv = LIBCAMERA_VIDEODEV(vd);
    GStreamerVideodev *gv = &lv->parent;
    const char *cam_name = qemu_opt_get(opts, "camera-name");
    char *caps = video_gstreamer_qemu_opt_get(opts, "caps");
    char *pipeline_desc = NULL;
    GError *error = NULL;
    GstStateChangeReturn ret;

    if (cam_name == NULL) {
        vd_error_setg(vd, errp, QERR_MISSING_PARAMETER, "camera-name");
        return VIDEODEV_RC_ERROR;
    }

    if (caps == NULL) {
        vd_error_setg(vd, errp, QERR_MISSING_PARAMETER, "caps");
        return VIDEODEV_RC_ERROR;
    }

    pipeline_desc = video_libcamera_pipeline_string(cam_name, caps);
    g_free(caps);
    if (!pipeline_desc) {
        vd_error_setg(vd, errp, "memory allocation failure");
        return VIDEODEV_RC_ERROR;
    }

    if (!gst_is_initialized()) {
        gst_init(NULL, NULL);
    }

    gv->pipeline = gst_parse_bin_from_description(pipeline_desc, false, &error);
    g_free(pipeline_desc);
    if (error) {
        vd_error_setg(vd, errp, "unable to parse pipeline: %s", error->message);
        return VIDEODEV_RC_ERROR;
    }

    gv->head = gst_bin_get_by_name(GST_BIN(gv->pipeline), "qemu_src");
    if (unlikely(!gv->head)) {
        vd_error_setg(vd, errp, "qemu_src not found");
        return VIDEODEV_RC_ERROR;
    }

    gv->tail = gst_bin_get_by_name(GST_BIN(gv->pipeline), "qemu_vc");
    if (unlikely(!gv->tail)) {
        vd_error_setg(vd, errp, "qemu_vc not found");
        return VIDEODEV_RC_ERROR;
    }

    gv->filter = gst_bin_get_by_name(GST_BIN(gv->pipeline), "qemu_cf");
    if (unlikely(!gv->filter)) {
        vd_error_setg(vd, errp, "qemu_cf not found");
        return VIDEODEV_RC_ERROR;
    }

    gv->sink = gst_bin_get_by_name(GST_BIN(gv->pipeline), "qemu_sink");
    if (unlikely(!gv->sink)) {
        vd_error_setg(vd, errp, "qemu_sink not found");
        return VIDEODEV_RC_ERROR;
    }

    ret = gst_element_set_state(gv->pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {

        vd_error_setg(vd, errp, "failed to set pipeline to READY");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static void video_libcamera_class_init(ObjectClass *oc, const void *data)
{
    VideodevClass *vc;
    video_gstreamer_class_init(oc, data);

    vc = VIDEODEV_CLASS(oc);

    /* override GStreamer class methods */
    vc->open          = video_libcamera_open;
    vc->enum_controls = NULL;
    vc->set_control   = NULL;
}

static const TypeInfo video_libcamera_type_info = {
    .name = TYPE_VIDEODEV_LIBCAMERA,
    .parent = TYPE_VIDEODEV_GSTREAMER,
    .instance_size = sizeof(LibcameraVideodev),
    .class_init = video_libcamera_class_init,
};

static void register_types(void)
{
    type_register_static(&video_libcamera_type_info);
}

type_init(register_types);
