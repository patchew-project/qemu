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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option.h"
#include "video/video.h"
#include "video/gstreamer-common.h"

typedef struct {
    const char *format;
    uint32_t fourcc;
} FormatFourCC;

FormatFourCC formatFourCCMap[] = {
    {"YUY2", QEMU_VIDEO_PIX_FMT_YUYV},
};

typedef struct VideoGStreamerCtrl {
    VideoControlType q;
    const char *v;
} VideoGStreamerCtrl;

static VideoGStreamerCtrl video_gstreamer_ctrl_table[] = {
    {
        .q = VideoControlTypeBrightness,
        .v = "brightness"
    },
    {
        .q = VideoControlTypeContrast,
        .v = "contrast"
    },
    {
        .q = VideoControlTypeHue,
        .v = "hue"
    },
    {
        .q = VideoControlTypeSaturation,
        .v = "saturation"
    }
};

static const char *video_qemu_control_to_gstreamer(VideoControlType type)
{
    for (int i = 0; i < ARRAY_SIZE(video_gstreamer_ctrl_table); i++) {

        if (video_gstreamer_ctrl_table[i].q == type) {
            return video_gstreamer_ctrl_table[i].v;
        }
    }

    return NULL;
}

static GstElement *video_gstreamer_pipeline_head(GstElement *tail)
{
    GstElement *current = tail;

    while (true) {

        GstPad *sink_pad, *peer_pad;
        GstElement *prev;

        sink_pad = gst_element_get_static_pad(current, "sink");
        if (!sink_pad) {
            /* no sink pad - source/head found */
            break;
        }

        if (!gst_pad_is_linked(sink_pad)) {
            /* unlinked sink pad - not a proper source */
            gst_object_unref(sink_pad);
            return NULL;
        }

        peer_pad = gst_pad_get_peer(sink_pad);
        gst_object_unref(sink_pad);
        if (!peer_pad) {
            /* broken pipeline? */
            return NULL;
        }

        prev = gst_pad_get_parent_element(peer_pad);
        gst_object_unref(peer_pad);
        if (!prev) {
            /* broken pipeline? */
            return NULL;
        }

        current = prev;
    }

    return current;
}

char *video_gstreamer_qemu_opt_get(QemuOpts *opts, const char *name)
{
    const char *qemu_opt = qemu_opt_get(opts, name);

    /*
     * QEMU's option parser forbids ',' inside option values,
     * making it hard to pass full GStreamer pipelines over the cmdline.
     *
     * Users replace ',' with '^' as a workaround. This function reverses
     * that replacement to restore the original pipeline.
     *
     * Use it whenever you would expect ',' within your option value.
     */

    if (qemu_opt == NULL) {
        return NULL;
    }

    char *sanitized_opt = g_strdup(qemu_opt);
    g_strdelimit(sanitized_opt, "^", ',');
    return sanitized_opt;
}

static int video_gstreamer_open(Videodev *vd, QemuOpts *opts, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    char *pipeline = video_gstreamer_qemu_opt_get(opts, "pipeline");
    GstStateChangeReturn ret;
    GstPad *tail_src_pad;
    GError *error = NULL;

    if (pipeline == NULL) {
        vd_error_setg(vd, errp, QERR_MISSING_PARAMETER, "pipeline");
        return VIDEODEV_RC_ERROR;
    }

    if (!gst_is_initialized())
        gst_init(NULL, NULL);

    gv->pipeline = gst_parse_bin_from_description(pipeline, false, &error);
    g_free(pipeline);
    if (error) {
        vd_error_setg(vd, errp, "unable to parse pipeline: %s", error->message);
        return VIDEODEV_RC_ERROR;
    }

    tail_src_pad = gst_bin_find_unlinked_pad(GST_BIN(gv->pipeline), GST_PAD_SRC);
    if (!tail_src_pad) {
        vd_error_setg(vd, errp, "pipeline has no unlinked src pad");
        return VIDEODEV_RC_ERROR;
    }

    gv->tail = gst_pad_get_parent_element(tail_src_pad);
    gst_object_unref(tail_src_pad);
    if (!gv->tail) {
        vd_error_setg(vd, errp, "failed to get pipeline's tail element");
        return VIDEODEV_RC_ERROR;
    }

    gv->head = video_gstreamer_pipeline_head(gv->tail);
    if (!gv->head) {
        vd_error_setg(vd, errp, "failed to get pipeline's head element");
        return VIDEODEV_RC_ERROR;
    }

    gv->filter = gst_element_factory_make("capsfilter", "filter");
    if (!gv->filter) {
        vd_error_setg(vd, errp, "failed to create capsfilter");
        return VIDEODEV_RC_ERROR;
    }

    gst_bin_add(GST_BIN(gv->pipeline), gv->filter);

    if (!gst_element_link(gv->tail, gv->filter)) {
        vd_error_setg(vd, errp, "failed to link pipeline to capsfilter");
        return VIDEODEV_RC_ERROR;
    }

    gv->sink = gst_element_factory_make("appsink", "sink");
    if (!gv->sink) {
        vd_error_setg(vd, errp, "failed to create appsink");
        return VIDEODEV_RC_ERROR;
    }

    gst_bin_add(GST_BIN(gv->pipeline), gv->sink);

    if (!gst_element_link(gv->filter, gv->sink)) {
        vd_error_setg(vd, errp, "failed to link pipeline to appsink");
        return VIDEODEV_RC_ERROR;
    }

    ret = gst_element_set_state(gv->pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {

        vd_error_setg(vd, errp, "failed to set pipeline to READY");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static uint32_t gst_format_to_fourcc(const char *format)
{
    if (!format) {
        return 0;
    }

    for (int i = 0; i < ARRAY_SIZE(formatFourCCMap); i++) {

        if (!strcmp(formatFourCCMap[i].format, format)) {
            return formatFourCCMap[i].fourcc;
        }
    }

    return 0;
}

static const char *gst_fourcc_to_format(const uint32_t fourcc) {

    for (int i = 0; i < ARRAY_SIZE(formatFourCCMap); i++) {

        if (formatFourCCMap[i].fourcc == fourcc) {
            return formatFourCCMap[i].format;
        }
    }

    return NULL;
}

static int video_gstreamer_enum_modes(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstPad *tail_src_pad = NULL;
    GstCaps *tail_src_caps = NULL;
    const GstStructure *s;
    uint32_t pixelformat;

    VideoMode *mode;
    VideoFramesize *frmsz;
    VideoFramerate *frmival;

    int i, j;
    const gchar *name, *format;
    const GValue *width_val, *height_val, *framerates;

    tail_src_pad = gst_element_get_static_pad(gv->tail, "src");
    if (!tail_src_pad) {
        vd_error_setg(vd, errp, "failed to get src pad");
        return VIDEODEV_RC_ERROR;
    }

    tail_src_caps = gst_pad_query_caps(tail_src_pad, NULL);
    if (!tail_src_caps) {
        vd_error_setg(vd, errp, "failed to get capabilities from src pad");
        return VIDEODEV_RC_ERROR;
    }

    for (i = 0; i < gst_caps_get_size(tail_src_caps); i++) {
        s = gst_caps_get_structure(tail_src_caps, i);

        name = gst_structure_get_name(s);
        if (strcmp(name, "video/x-raw") != 0)
            continue;

        format = gst_structure_get_string(s, "format");
        if (!format)
            continue;

        pixelformat = gst_format_to_fourcc(format);
        if (pixelformat == 0)
            continue;

        if (!gst_structure_has_field(s, "width") ||
            !gst_structure_has_field(s, "height") ||
            !gst_structure_has_field(s, "framerate"))
            continue;

        width_val = gst_structure_get_value(s, "width");
        height_val = gst_structure_get_value(s, "height");
        framerates = gst_structure_get_value(s, "framerate");

        if (GST_VALUE_HOLDS_INT_RANGE(width_val) ||
            GST_VALUE_HOLDS_INT_RANGE(height_val))
            continue;

        // Collect all width values
        GArray *widths = g_array_new(FALSE, FALSE, sizeof(int));
        if (G_VALUE_TYPE(width_val) == G_TYPE_INT) {
            int w = g_value_get_int(width_val);
            g_array_append_val(widths, w);
        } else if (GST_VALUE_HOLDS_LIST(width_val)) {
            for (j = 0; j < gst_value_list_get_size(width_val); j++) {
                const GValue *v = gst_value_list_get_value(width_val, j);
                if (G_VALUE_TYPE(v) == G_TYPE_INT) {
                    int w = g_value_get_int(v);
                    g_array_append_val(widths, w);
                }
            }
        } else {
            g_array_free(widths, TRUE);
            continue;
        }

        // Collect all height values
        GArray *heights = g_array_new(FALSE, FALSE, sizeof(int));
        if (G_VALUE_TYPE(height_val) == G_TYPE_INT) {
            int h = g_value_get_int(height_val);
            g_array_append_val(heights, h);
        } else if (GST_VALUE_HOLDS_LIST(height_val)) {
            for (j = 0; j < gst_value_list_get_size(height_val); j++) {
                const GValue *v = gst_value_list_get_value(height_val, j);
                if (G_VALUE_TYPE(v) == G_TYPE_INT) {
                    int h = g_value_get_int(v);
                    g_array_append_val(heights, h);
                }
            }
        } else {
            g_array_free(widths, TRUE);
            g_array_free(heights, TRUE);
            continue;
        }

        // Iterate over all width × height combinations
        for (int wi = 0; wi < widths->len; wi++) {
            for (int hi = 0; hi < heights->len; hi++) {
                int w = g_array_index(widths, int, wi);
                int h = g_array_index(heights, int, hi);

                // Find or create VideoMode for this pixelformat
                mode = NULL;
                for (j = 0; j < vd->nmodes; j++) {
                    if (vd->modes[j].pixelformat == pixelformat) {
                        mode = &vd->modes[j];
                        break;
                    }
                }

                if (!mode) {
                    vd->nmodes++;
                    vd->modes = g_realloc(vd->modes, vd->nmodes * sizeof(VideoMode));
                    mode = &vd->modes[vd->nmodes - 1];
                    mode->pixelformat = pixelformat;
                    mode->framesizes = NULL;
                    mode->nframesize = 0;
                }

                // Add new framesize
                mode->nframesize++;
                mode->framesizes = g_realloc(mode->framesizes,
                                             mode->nframesize * sizeof(VideoFramesize));
                frmsz = &mode->framesizes[mode->nframesize - 1];

                frmsz->width = w;
                frmsz->height = h;
                frmsz->framerates = NULL;
                frmsz->nframerate = 0;

                // Handle framerates (list or single fraction)
                if (GST_VALUE_HOLDS_LIST(framerates)) {
                    for (j = 0; j < gst_value_list_get_size(framerates); j++) {
                        const GValue *fval = gst_value_list_get_value(framerates, j);
                        if (GST_VALUE_HOLDS_FRACTION(fval)) {
                            frmsz->nframerate++;
                            frmsz->framerates = g_realloc(frmsz->framerates,
                                                          frmsz->nframerate * sizeof(VideoFramerate));
                            frmival = &frmsz->framerates[frmsz->nframerate - 1];

                            // intentionally swapped
                            frmival->denominator = gst_value_get_fraction_numerator(fval);
                            frmival->numerator = gst_value_get_fraction_denominator(fval);
                        }
                    }
                } else if (GST_VALUE_HOLDS_FRACTION(framerates)) {
                    frmsz->nframerate++;
                    frmsz->framerates = g_realloc(frmsz->framerates,
                                                  frmsz->nframerate * sizeof(VideoFramerate));
                    frmival = &frmsz->framerates[frmsz->nframerate - 1];

                    // intentionally swapped
                    frmival->denominator = gst_value_get_fraction_numerator(framerates);
                    frmival->numerator = gst_value_get_fraction_denominator(framerates);
                }
            }
        }

        g_array_free(widths, TRUE);
        g_array_free(heights, TRUE);
    }

    if (vd->modes == NULL) {
        vd_error_setg(vd, errp, "failed to enumerate modes");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_set_options(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    const char *pixformat;
    GstCaps *caps;

    if ((pixformat = gst_fourcc_to_format(vd->selected.mode->pixelformat)) == NULL) {

        vd_error_setg(vd, errp, "unsupported pixelformat");
        return VIDEODEV_RC_ERROR;
    }

    caps = gst_caps_new_simple(
        "video/x-raw",
        "width",     G_TYPE_INT,        vd->selected.frmsz->width,
        "height",    G_TYPE_INT,        vd->selected.frmsz->height,
        "format",    G_TYPE_STRING,     pixformat,
        "framerate", GST_TYPE_FRACTION, vd->selected.frmrt.denominator,
                                        vd->selected.frmrt.numerator, NULL
    );

    if (caps == NULL) {

        vd_error_setg(vd, errp, "failed to create new caps");
        return VIDEODEV_RC_ERROR;
    }

    g_object_set(gv->filter, "caps", caps, NULL);
    gst_caps_unref(caps);

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_stream_on(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstStateChangeReturn ret;

    if (gv->pipeline == NULL) {

        vd_error_setg(vd, errp, "GStreamer pipeline not initialized!");
        return VIDEODEV_RC_ERROR;
    }

    if (video_gstreamer_set_options(vd, errp) != VIDEODEV_RC_OK) {
        return VIDEODEV_RC_ERROR;
    }

    ret = gst_element_set_state(gv->pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {

        vd_error_setg(vd, errp, "failed to start GStreamer pipeline!");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_stream_off(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstStateChangeReturn ret;

    if (gv->pipeline == NULL) {

        vd_error_setg(vd, errp, "GStreamer pipeline not initialized!");
        return VIDEODEV_RC_ERROR;
    }

    ret = gst_element_set_state(gv->pipeline, GST_STATE_READY);

    if (ret == GST_STATE_CHANGE_FAILURE) {

        vd_error_setg(vd, errp, "failed to stop GStreamer pipeline!");
        return VIDEODEV_RC_ERROR;
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_claim_frame(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GstSample *sample;
    GstBuffer *buffer;

    if ((sample = gst_app_sink_try_pull_sample(GST_APP_SINK(gv->sink), 0)) == NULL) {

        vd_error_setg(vd, errp, "appsink: underrun");
        return VIDEODEV_RC_UNDERRUN;
    }

    if ((buffer = gst_sample_get_buffer(sample)) == NULL) {

        gst_sample_unref(sample);
        vd_error_setg(vd, errp, "could not retrieve sample buffer");
        return VIDEODEV_RC_ERROR;
    }

    if (gst_buffer_map(buffer, &gv->current_frame.map_info, GST_MAP_READ) != TRUE) {

        gst_sample_unref(sample);
        vd_error_setg(vd, errp, "could not map sample buffer");
        return VIDEODEV_RC_ERROR;
    }

    gv->current_frame.sample     = sample;
    gv->current_frame.buffer     = buffer;
    vd->current_frame.data       = (uint8_t*) gv->current_frame.map_info.data;
    vd->current_frame.bytes_left = gv->current_frame.map_info.size;

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_release_frame(Videodev *vd, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);

    gst_buffer_unmap(gv->current_frame.buffer, &gv->current_frame.map_info);
    gst_sample_unref(gv->current_frame.sample);

    gv->current_frame.sample     = NULL;
    gv->current_frame.buffer     = NULL;
    vd->current_frame.data       = NULL;
    vd->current_frame.bytes_left = 0;

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_probe_control(Videodev *vd, VideoGStreamerCtrl *ctrl, VideoControl *c)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    GParamSpec *pspec;
    GParamSpecInt *ispec;

    /*
     * Apparently there is no proper way to find out
     * the real minimum and maximum of a video control.
     *
     * There is GParamSpec, but that one only gives us
     * min and max of the underlying datatype.
     *
     * As a workaround, we could dynamically probe accepted
     * values for a given control using g_object_get/g_object_set,
     * but that might be an idea for the future.
     */

    pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(gv->head), ctrl->v);

    if (pspec == NULL) {
        return VIDEODEV_RC_NOTSUP;
    }

    ispec = G_PARAM_SPEC_INT(pspec);

    *c = (VideoControl) {

        .type = ctrl->q,
        .min  = ispec->minimum,
        .max  = ispec->maximum,
        .step = 1
    };

    g_object_get(G_OBJECT(gv->head), ctrl->v, &c->def, NULL);
    return VIDEODEV_RC_OK;
}

static int video_gstreamer_enum_controls(Videodev *vd, Error **errp)
{
    for (int i = 0; i < ARRAY_SIZE(video_gstreamer_ctrl_table); i++) {

        VideoGStreamerCtrl *ctrl;
        VideoControl ctrl_buffer;

        ctrl = &video_gstreamer_ctrl_table[i];

        if (video_gstreamer_probe_control(vd, ctrl, &ctrl_buffer) != VIDEODEV_RC_OK) {
            continue;
        }

        vd->ncontrols += 1;
        vd->controls   = g_realloc(vd->controls, vd->ncontrols * sizeof(VideoControl));

        vd->controls[vd->ncontrols - 1] = ctrl_buffer;
    }

    return VIDEODEV_RC_OK;
}

static int video_gstreamer_set_control(Videodev *vd, VideoControl *ctrl, Error **errp)
{
    GStreamerVideodev *gv = GSTREAMER_VIDEODEV(vd);
    const char *property;
    int value;

    if ((property = video_qemu_control_to_gstreamer(ctrl->type)) == NULL) {

        vd_error_setg(vd, errp, "invalid control property!");
        return VIDEODEV_RC_INVAL;
    }

    g_object_set(G_OBJECT(gv->head), property, ctrl->cur, NULL);
    g_object_get(G_OBJECT(gv->head), property, &value, NULL);

    if (value != ctrl->cur) {

        vd_error_setg(vd, errp, "could not apply new setting for '%s'", property);
        return VIDEODEV_RC_INVAL;
    }

    return VIDEODEV_RC_OK;
}

void video_gstreamer_class_init(ObjectClass *oc, const void *data)
{
    VideodevClass *vc = VIDEODEV_CLASS(oc);

    vc->open          = video_gstreamer_open;
    vc->enum_modes    = video_gstreamer_enum_modes;
    vc->stream_on     = video_gstreamer_stream_on;
    vc->stream_off    = video_gstreamer_stream_off;
    vc->claim_frame   = video_gstreamer_claim_frame;
    vc->release_frame = video_gstreamer_release_frame;
    vc->enum_controls = video_gstreamer_enum_controls;
    vc->set_control   = video_gstreamer_set_control;
}

static const TypeInfo video_gstreamer_type_info = {
    .name = TYPE_VIDEODEV_GSTREAMER,
    .parent = TYPE_VIDEODEV,
    .instance_size = sizeof(GStreamerVideodev),
    .class_init = video_gstreamer_class_init,
};

static void register_types(void)
{
    type_register_static(&video_gstreamer_type_info);
}

type_init(register_types);
