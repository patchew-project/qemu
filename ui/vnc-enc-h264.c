/*
 * QEMU VNC display driver: H264 encoding
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "vnc.h"

#include <gst/gst.h>

static char *get_available_encoder(const char *encoder_list)
{
    int i = 0;
    char *ret = NULL;
    char **encoder_array = NULL;
    const char *encoder_name = NULL;

    g_assert(encoder_list != NULL);

    if (!strcmp(encoder_list, "")) {
        /* use default list */
        encoder_list = "nvh264enc:vaapih264enc:x264enc:openh264enc";
    }

    encoder_array = g_strsplit(encoder_list, ":", -1);

    while ((encoder_name = encoder_array[i])) {
        GstElement *element = gst_element_factory_make(
            encoder_name, "video-encoder");
        if (element != NULL) {
            gst_object_unref(element);
            ret = strdup(encoder_name);
            break;
        }
        i++;
    }

    g_strfreev(encoder_array);

    return ret;
}

static GstElement *create_encoder(const char *encoder_name)
{
    GstElement *encoder = gst_element_factory_make(
        encoder_name, "video-encoder");
    if (!encoder) {
        VNC_DEBUG("Could not create gst '%s' video encoder\n", encoder_name);
        return NULL;
    }

    if (!strcmp(encoder_name, "nvh264enc")) {
        g_object_set(
            encoder,
            "preset", 8,         /* p1 - fastest */
            "multi-pass", 1,     /* multipass disabled */
            "tune", 2,           /* low latency */
            "zerolatency", true, /* low latency */
            /* avoid access unit delimiters (Nal Unit Type 9) - not required */
            "aud", false,
            NULL);
    } else if (!strcmp(encoder_name, "vaapih264enc")) {
        g_object_set(encoder, "tune", 1, NULL); /* high compression */
    } else if (!strcmp(encoder_name, "x264enc")) {
        g_object_set(
            encoder,
            "tune", 4, /* zerolatency */
            /*
             * fix for zerolatency with novnc (without,
             * noVNC displays green stripes)
             */
            "threads", 1,
            "pass", 5, /* Constant Quality */
            "quantizer", 26,
            /* avoid access unit delimiters (Nal Unit Type 9) - not required */
            "aud", false,
            NULL);
    } else if (!strcmp(encoder_name, "openh264enc")) {
        g_object_set(
            encoder,
            "usage-type", 1, /* screen content */
            "complexity", 0, /* low, high speed */
            "rate-control", 0, /* quality mode */
            "qp-min", 20,
            "qp-max", 27,
            NULL);
    } else {
        VNC_DEBUG("Unknown H264 encoder name '%s' - not setting any properties",
            encoder_name);
    }

    return encoder;
}

static void destroy_encoder_context(VncState *vs)
{
    GstStateChangeReturn state_change_ret;

    VNC_DEBUG("Destroy h264 context.\n");

    /*
     * Some encoders can hang indefinitely (i.e. nvh264enc) if
     * the pipeline is not stopped before it is destroyed
     * (Observed on Debian bookworm).
     */
    if (vs->h264->pipeline != NULL) {
        state_change_ret = gst_element_set_state(
            vs->h264->pipeline, GST_STATE_NULL);

        if (state_change_ret == GST_STATE_CHANGE_FAILURE) {
            VNC_DEBUG("Unable to stop the GST pipeline\n");
        }
    }

    gst_clear_object(&vs->h264->source);
    gst_clear_object(&vs->h264->convert);
    gst_clear_object(&vs->h264->gst_encoder);
    gst_clear_object(&vs->h264->sink);
    gst_clear_object(&vs->h264->pipeline);
}

static bool create_encoder_context(VncState *vs, int w, int h)
{
    g_autoptr(GstCaps) source_caps = NULL;
    GstStateChangeReturn state_change_ret;

    g_assert(vs->h264 != NULL);

    if (vs->h264->sink && w == vs->h264->width && h == vs->h264->height) {
        return TRUE;
    }

    destroy_encoder_context(vs);

    vs->h264->width = w;
    vs->h264->height = h;

    vs->h264->source = gst_element_factory_make("appsrc", "source");
    if (!vs->h264->source) {
        VNC_DEBUG("Could not create gst source\n");
        goto error;
    }

    vs->h264->convert = gst_element_factory_make("videoconvert", "convert");
    if (!vs->h264->convert) {
        VNC_DEBUG("Could not create gst convert element\n");
        goto error;
    }

    vs->h264->gst_encoder = create_encoder(vs->h264->encoder_name);
    if (!vs->h264->gst_encoder) {
        VNC_DEBUG("Could not create gst x264 encoder\n");
        goto error;
    }

    vs->h264->sink = gst_element_factory_make("appsink", "sink");
    if (!vs->h264->sink) {
        VNC_DEBUG("Could not create gst sink\n");
        goto error;
    }

    vs->h264->pipeline = gst_pipeline_new("vnc-h264-pipeline");
    if (!vs->h264->pipeline) {
        VNC_DEBUG("Could not create gst pipeline\n");
        goto error;
    }

    gst_object_ref(vs->h264->source);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->source)) {
        gst_object_unref(vs->h264->source);
        VNC_DEBUG("Could not add source to gst pipeline\n");
        goto error;
    }

    gst_object_ref(vs->h264->convert);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->convert)) {
        gst_object_unref(vs->h264->convert);
        VNC_DEBUG("Could not add convert to gst pipeline\n");
        goto error;
    }

    gst_object_ref(vs->h264->gst_encoder);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->gst_encoder)) {
        gst_object_unref(vs->h264->gst_encoder);
        VNC_DEBUG("Could not add encoder to gst pipeline\n");
        goto error;
    }

    gst_object_ref(vs->h264->sink);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->sink)) {
        gst_object_unref(vs->h264->sink);
        VNC_DEBUG("Could not add sink to gst pipeline\n");
        goto error;
    }

    source_caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "BGRx",
        "framerate", GST_TYPE_FRACTION, 33, 1,
        "width", G_TYPE_INT, w,
        "height", G_TYPE_INT, h,
        NULL);

    if (!source_caps) {
        VNC_DEBUG("Could not create source caps filter\n");
        goto error;
    }

    g_object_set(vs->h264->source, "caps", source_caps, NULL);

    if (gst_element_link_many(
            vs->h264->source,
            vs->h264->convert,
            vs->h264->gst_encoder,
            vs->h264->sink,
            NULL
        ) != TRUE) {
        VNC_DEBUG("Elements could not be linked.\n");
        goto error;
    }

    /* Start playing */
    state_change_ret = gst_element_set_state(
        vs->h264->pipeline, GST_STATE_PLAYING);

    if (state_change_ret == GST_STATE_CHANGE_FAILURE) {
        VNC_DEBUG("Unable to set the pipeline to the playing state.\n");
        goto error;
    }

    return TRUE;

 error:
    destroy_encoder_context(vs);
    return FALSE;
}

bool vnc_h264_encoder_init(VncState *vs)
{
    char *encoder_name;

    g_assert(vs->h264 == NULL);
    g_assert(vs->vd != NULL);
    g_assert(vs->vd->h264_encoder_list != NULL);

    encoder_name = get_available_encoder(vs->vd->h264_encoder_list);
    if (encoder_name == NULL) {
        VNC_DEBUG("No H264 encoder available.\n");
        return false;
    }

    vs->h264 = g_new0(VncH264, 1);
    vs->h264->encoder_name = encoder_name;

    VNC_DEBUG("Allow H264 using encoder '%s`\n", encoder_name);

    return true;
}

/*
 * Returns the number of generated framebuffer updates,
 * or -1 in case of errors
 */
int vnc_h264_send_framebuffer_update(VncState *vs)
{
    int n = 0;
    int rdb_h264_flags = 0;
    int width, height;
    uint8_t *src_data_ptr = NULL;
    size_t src_data_size;
    GstFlowReturn flow_ret = GST_FLOW_ERROR;
    GstBuffer *src_buffer = NULL;

    g_assert(vs->h264 != NULL);
    g_assert(vs->vd != NULL);
    g_assert(vs->vd->server != NULL);

    width = pixman_image_get_width(vs->vd->server);
    height = pixman_image_get_height(vs->vd->server);

    g_assert(width == vs->client_width);
    g_assert(height == vs->client_height);

    if (vs->h264->sink) {
        if (width != vs->h264->width || height != vs->h264->height) {
            rdb_h264_flags = 2;
        }
    } else {
        rdb_h264_flags = 2;
    }

    if (!create_encoder_context(vs, width, height)) {
        VNC_DEBUG("Create encoder context failed\n");
        return -1;
    }

    g_assert(vs->h264->sink != NULL);

    src_data_ptr = vnc_server_fb_ptr(vs->vd, 0, 0);
    src_data_size = width * height * VNC_SERVER_FB_BYTES;

    src_buffer = gst_buffer_new_wrapped_full(
        0, src_data_ptr, src_data_size, 0, src_data_size, NULL, NULL);

    g_signal_emit_by_name(
        vs->h264->source, "push-buffer", src_buffer, &flow_ret);

    if (flow_ret != GST_FLOW_OK) {
        VNC_DEBUG("gst appsrc push buffer failed\n");
        return -1;
    }

    do {
        GstSample *sample = NULL;
        GstMapInfo map;
        GstBuffer *out_buffer;

        /* Retrieve the buffer */
        g_signal_emit_by_name(vs->h264->sink, "try-pull-sample", 0, &sample);
        if (!sample) {
            break;
        }
        out_buffer = gst_sample_get_buffer(sample);
        if (gst_buffer_map(out_buffer, &map, 0)) {
            vnc_framebuffer_update(vs, 0, 0, width, height, VNC_ENCODING_H264);
            vnc_write_s32(vs, map.size); /* write data length */
            vnc_write_s32(vs, rdb_h264_flags); /* write flags */
            rdb_h264_flags = 0;

            VNC_DEBUG("GST vnc_h264_update send %ld\n", map.size);

            vnc_write(vs, map.data, map.size);

            gst_buffer_unmap(out_buffer, &map);

            n += 1;
        } else {
            VNC_DEBUG("unable to map sample\n");
        }
        gst_sample_unref(sample);
    } while (true);

    return n;
}

void vnc_h264_clear(VncState *vs)
{
    if (!vs->h264) {
        return;
    }

    destroy_encoder_context(vs);
    g_free(vs->h264->encoder_name);

    g_clear_pointer(&vs->h264, g_free);
}
