#include "qemu/osdep.h"
#include "vnc.h"

#include <gst/gst.h>

static void destroy_encoder_context(VncState *vs)
{
    if (!vs->h264) {
        return;
    }

    if (vs->h264->source) {
        gst_object_unref(vs->h264->source);
        vs->h264->source = NULL;
    }

    if (vs->h264->convert) {
        gst_object_unref(vs->h264->convert);
        vs->h264->convert = NULL;
    }

    if (vs->h264->gst_encoder) {
        gst_object_unref(vs->h264->gst_encoder);
        vs->h264->sink = NULL;
    }

    if (vs->h264->sink) {
        gst_object_unref(vs->h264->sink);
        vs->h264->sink = NULL;
    }

    if (vs->h264->pipeline) {
        gst_object_unref(vs->h264->pipeline);
        vs->h264->pipeline = NULL;
    }
}

static bool create_encoder_context(VncState *vs, int w, int h)
{
    g_assert(vs->h264 != NULL);

    if (vs->h264->sink) {
        if (w != vs->h264->width || h != vs->h264->height) {
            destroy_encoder_context(vs);
        }
    }

    if (vs->h264->sink) {
        return TRUE;
    }

    vs->h264->width = w;
    vs->h264->height = h;

    vs->h264->source = gst_element_factory_make("appsrc", "source");
    if (!vs->h264->source) {
        VNC_DEBUG("Could not create gst source\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    vs->h264->convert = gst_element_factory_make("videoconvert", "convert");
    if (!vs->h264->convert) {
        VNC_DEBUG("Could not create gst convert element\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    vs->h264->gst_encoder = gst_element_factory_make("x264enc", "gst-encoder");
    if (!vs->h264->gst_encoder) {
        VNC_DEBUG("Could not create gst x264 encoder\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    g_object_set(vs->h264->gst_encoder, "tune", 4, NULL); /* zerolatency */
    /* fix for zerolatency with novnc (without, noVNC displays green stripes) */
    g_object_set(vs->h264->gst_encoder, "threads", 1, NULL);

    g_object_set(vs->h264->gst_encoder, "pass", 5, NULL); /* Constant Quality */
    g_object_set(vs->h264->gst_encoder, "quantizer", 26, NULL);

    /* avoid access unit delimiters (Nal Unit Type 9) - not required */
    g_object_set(vs->h264->gst_encoder, "aud", false, NULL);

    vs->h264->sink = gst_element_factory_make("appsink", "sink");
    if (!vs->h264->sink) {
        VNC_DEBUG("Could not create gst sink\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    vs->h264->pipeline = gst_pipeline_new("vnc-h264-pipeline");
    if (!vs->h264->pipeline) {
        VNC_DEBUG("Could not create gst pipeline\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    gst_object_ref(vs->h264->source);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->source)) {
        gst_object_unref(vs->h264->source);
        VNC_DEBUG("Could not add source to gst pipeline\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    gst_object_ref(vs->h264->convert);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->convert)) {
        gst_object_unref(vs->h264->convert);
        VNC_DEBUG("Could not add convert to gst pipeline\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    gst_object_ref(vs->h264->gst_encoder);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->gst_encoder)) {
        gst_object_unref(vs->h264->gst_encoder);
        VNC_DEBUG("Could not add encoder to gst pipeline\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    gst_object_ref(vs->h264->sink);
    if (!gst_bin_add(GST_BIN(vs->h264->pipeline), vs->h264->sink)) {
        gst_object_unref(vs->h264->sink);
        VNC_DEBUG("Could not add sink to gst pipeline\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    GstCaps *source_caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "BGRx",
        "framerate", GST_TYPE_FRACTION, 33, 1,
        "width", G_TYPE_INT, w,
        "height", G_TYPE_INT, h,
        NULL);

    if (!source_caps) {
        VNC_DEBUG("Could not create source caps filter\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    g_object_set(vs->h264->source, "caps", source_caps, NULL);
    gst_caps_unref(source_caps);

    if (gst_element_link_many(
            vs->h264->source,
            vs->h264->convert,
            vs->h264->gst_encoder,
            vs->h264->sink,
            NULL
        ) != TRUE) {
        VNC_DEBUG("Elements could not be linked.\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    /* Start playing */
    int ret = gst_element_set_state(vs->h264->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        VNC_DEBUG("Unable to set the pipeline to the playing state.\n");
        destroy_encoder_context(vs);
        return FALSE;
    }

    return TRUE;
}

int vnc_h264_encoder_init(VncState *vs)
{
    g_assert(vs->h264 == NULL);

    vs->h264 = g_malloc0(sizeof(VncH264));

    return 0;
}

int vnc_h264_send_framebuffer_update(
    VncState *vs,
    int _x,
    int _y,
    int _w,
    int _h
) {
    g_assert(vs->h264 != NULL);
    g_assert(vs->vd != NULL);
    g_assert(vs->vd->server != NULL);

    int width = pixman_image_get_width(vs->vd->server);
    int height = pixman_image_get_height(vs->vd->server);

    g_assert(width == vs->client_width);
    g_assert(height == vs->client_height);

    int rdb_h264_flags = 0;

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

    uint8_t *src_data_ptr = vnc_server_fb_ptr(vs->vd, 0, 0);
    size_t src_data_size = width * height * VNC_SERVER_FB_BYTES;

    GstBuffer *buffer = gst_buffer_new_wrapped_full(
        0, src_data_ptr, src_data_size, 0, src_data_size, NULL, NULL);

    GstFlowReturn ret;
    g_signal_emit_by_name(vs->h264->source, "push-buffer", buffer, &ret);

    if (ret != GST_FLOW_OK) {
        VNC_DEBUG("gst appsrc push buffer failed\n");
        return -1;
    }

    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(vs->h264->sink, "pull-sample", &sample);
    if (sample) {
        GstBuffer *buf = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (gst_buffer_map(buf, &map, 0)) {
            vnc_framebuffer_update(vs, 0, 0, width, height, VNC_ENCODING_H264);
            vnc_write_s32(vs, map.size); /* write data length */
            vnc_write_s32(vs, rdb_h264_flags); /* write flags */
            rdb_h264_flags = 0;

            VNC_DEBUG("GST vnc_h264_update send %ld\n", map.size);

            vnc_write(vs, map.data, map.size);

            gst_buffer_unmap(buf, &map);

            return 1;
        } else {
            VNC_DEBUG("unable to map sample\n");
        }
        gst_sample_unref(sample);
        return 1;
    } else {
        VNC_DEBUG("gst no data\n");
        return 0;
    }
}

void vnc_h264_clear(VncState *vs)
{
    if (!vs->h264) {
        return;
    }

    destroy_encoder_context(vs);

    g_free(vs->h264);
    vs->h264 = NULL;
}
