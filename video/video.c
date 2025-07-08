/*
 * Copyright 2025 9elements GmbH
 * Copyright 2021 Bytedance, Inc.
 *
 * Authors:
 *   David Milosevic <david.milosevic@9elements.com>
 *   Marcello Sylvester Bauer <marcello.bauer@9elements.com>
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "video/video.h"

static QLIST_HEAD(, Videodev) videodevs;

typedef struct VideodevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} VideodevClassFE;

static void videodev_class_foreach(ObjectClass *klass, void *opaque)
{
    VideodevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), "videodev-"));
    fe->fn(object_class_get_name(klass) + sizeof(TYPE_VIDEODEV), fe->opaque);
}

static void videodev_name_foreach(void (*fn)(const char *name, void *opaque), void *opaque)
{
    VideodevClassFE fe = { .fn = fn, .opaque = opaque };
    object_class_foreach(videodev_class_foreach, TYPE_VIDEODEV, false, &fe);
}

static void help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;
    g_string_append_printf(str, "\n%s", name);
}

static const VideodevClass *videodev_get_class(const char *backend, Error **errp)
{
    ObjectClass *oc;
    const VideodevClass *vc;
    char *typename = g_strdup_printf("videodev-%s", backend);

    oc = module_object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_VIDEODEV)) {

        error_setg(errp, "'%s' is not a valid videodev backend name", backend);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {

        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "backend", "a non-abstract device type");
        return NULL;
    }

    vc = VIDEODEV_CLASS(oc);
    return vc;
}

// @private
static inline bool videodev_frame_ready(Videodev *vd)
{
    return (vd->current_frame.data != NULL) && (vd->current_frame.bytes_left != 0);
}

// @private
static int videodev_claim_frame(Videodev *vd, Error **errp)
{
    VideodevClass *vc = VIDEODEV_GET_CLASS(vd);
    int rc;

    if (vc->claim_frame == NULL) {

        vd_error_setg(vd, errp, "missing 'claim_frame' method!");
        return VIDEODEV_RC_NOTSUP;
    }

    if ((rc = vc->claim_frame(vd, errp)) != VIDEODEV_RC_OK) {
        return rc;
    }

    /*
     * breaking this assertion means the backend
     * messed up. It did NOT initialize the current frame
     * properly despite returning VIDEODEV_RC_OK.
     *
     * The solution here is to fix the implementation
     * of claim_frame
     * */
    assert(videodev_frame_ready(vd) == true);
    return VIDEODEV_RC_OK;
}

// @private
static int videodev_release_frame(Videodev *vd, Error **errp)
{
    VideodevClass *vc = VIDEODEV_GET_CLASS(vd);
    int rc;

    if (vc->release_frame == NULL) {

        vd_error_setg(vd, errp, "missing 'release_frame' method!");
        return VIDEODEV_RC_NOTSUP;
    }

    if ((rc = vc->release_frame(vd, errp)) != VIDEODEV_RC_OK) {
        return rc;
    }

    /*
     * breaking this assertion means the backend
     * messed up. It did NOT release the current frame
     * properly despite returning VIDEODEV_RC_OK.
     *
     * The solution here is to fix the implementation
     * of release_frame
     * */
    assert(videodev_frame_ready(vd) == false);
    return VIDEODEV_RC_OK;
}

static void videodev_free_modes(Videodev *vd)
{
    if (vd->nmodes == 0)
        return;

    for (int i = 0; i < vd->nmodes; i++) {

        for (int j = 0; j < vd->modes[i].nframesize; j++) {

            g_free(vd->modes[i].framesizes[j].framerates);
        }

        g_free(vd->modes[i].framesizes);
    }

    g_free(vd->modes);
    vd->nmodes = 0;
    vd->modes  = NULL;
}

char *qemu_videodev_get_id(Videodev *vd)
{
    return vd->id;
}

Videodev *qemu_videodev_by_id(char *id, Error **errp)
{
    Videodev *vd;

    QLIST_FOREACH(vd, &videodevs, list) {
        if (strcmp(id, vd->id) == 0) {
            return vd;
        }
    }

    error_setg(errp, "videodev '%s' not found", id);
    return NULL;
}

void qemu_videodev_register(Videodev *vd, Error **errp)
{
    if (vd->registered == true) {

        error_setg(errp, "Videodev already registered");
        return;
    }

    vd->registered = true;
}

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp)
{
    Object *obj;
    Videodev *vd;
    const VideodevClass *vc;
    const char *name = qemu_opt_get(opts, "backend");
    const char *id = qemu_opts_id(opts);
    Error *local_err = NULL;

    if (name && is_help_option(name)) {

        GString *str = g_string_new("");
        videodev_name_foreach(help_string_append, str);

        qemu_printf("Available videodev backend types: %s\n", str->str);
        g_string_free(str, true);
        return NULL;
    }

    if (id == NULL) {

        error_setg(errp, QERR_MISSING_PARAMETER, "id");
        return NULL;
    }

    if (name == NULL) {

        error_setg(errp, "\"%s\" missing backend", qemu_opts_id(opts));
        return NULL;
    }

    if ((vc = videodev_get_class(name, &local_err)) == NULL) {
        goto propagate_error;
    }

    if (vc->open == NULL || vc->enum_modes == NULL) {

        error_setg(errp, "missing open/enum_modes method!");
        return NULL;
    }

    obj = object_new(object_class_get_name(OBJECT_CLASS(vc)));
    vd  = VIDEODEV(obj);

    vd->id = g_strdup(id);

    if (vc->open(vd, opts, &local_err) != VIDEODEV_RC_OK) {
        goto free_and_propagate_error;
    }

    if (vc->enum_modes(vd, &local_err) != VIDEODEV_RC_OK) {
        goto free_and_propagate_error;
    }

    if (vc->enum_controls) {

        if (vc->enum_controls(vd, &local_err) != VIDEODEV_RC_OK) {
            goto free_and_propagate_error;
        }
    }

    QLIST_INSERT_HEAD(&videodevs, vd, list);
    return vd;

free_and_propagate_error:
    g_free(vd->id);
    object_unref(vd);
propagate_error:
    error_propagate(errp, local_err);
    return NULL;
}

int qemu_videodev_delete(Videodev *vd, Error **errp)
{
    VideodevClass *vc = VIDEODEV_GET_CLASS(vd);

    if (vd->is_streaming == true) {
        qemu_videodev_stream_off(vd, NULL);
    }

    if (vc->close) {

        if (vc->close(vd, errp) != VIDEODEV_RC_OK) {
            return VIDEODEV_RC_ERROR;
        }
    }

    videodev_free_modes(vd);

    if (vd->controls != NULL) {
        g_free(vd->controls);
    }

    g_free(vd->id);
    QLIST_REMOVE(vd, list);
    object_unref(vd);

    return VIDEODEV_RC_OK;
}

int qemu_videodev_set_control(Videodev *vd, VideoControl *ctrl, Error **errp)
{
    VideodevClass *vc = VIDEODEV_GET_CLASS(vd);

    if (vc->set_control == NULL) {

        vd_error_setg(vd, errp, "missing 'set_control' method!");
        return VIDEODEV_RC_NOTSUP;
    }

    return vc->set_control(vd, ctrl, errp);
}

bool qemu_videodev_check_options(Videodev *vd, VideoStreamOptions *opts)
{
    if (opts->format_index >= vd->nmodes)
        return false;

    if (opts->frame_index >= vd->modes[opts->format_index].nframesize)
        return false;

    return true;
}

// @private
static int qemu_videodev_select_options(Videodev *vd, VideoStreamOptions *opts)
{
    if (qemu_videodev_check_options(vd, opts) == false) {
        return VIDEODEV_RC_ERROR;
    }

    vd->selected.mode  = &vd->modes[opts->format_index];
    vd->selected.frmsz = &vd->modes[opts->format_index].framesizes[opts->frame_index];

    vd->selected.frmrt.numerator   = 30; // prime number (2 * 3 * 5)
    vd->selected.frmrt.denominator = 30 * 10000000 / opts->frame_interval;

    return VIDEODEV_RC_OK;
}

int qemu_videodev_stream_on(Videodev *vd, VideoStreamOptions *opts, Error **errp)
{
    VideodevClass *vc = VIDEODEV_GET_CLASS(vd);
    int rc;

    if (vd->is_streaming == true) {

        vd_error_setg(vd, errp, "could not enable streaming. Already streaming!");
        return VIDEODEV_RC_ERROR;
    }

    if (qemu_videodev_select_options(vd, opts) != VIDEODEV_RC_OK) {

        vd_error_setg(vd, errp, "failed to select options - Invalid mode/framesize");
        return VIDEODEV_RC_INVAL;
    }

    if (vc->stream_on == NULL) {

        vd_error_setg(vd, errp, "missing 'stream_on' method!");
        return VIDEODEV_RC_NOTSUP;
    }

    if ((rc = vc->stream_on(vd, errp)) != VIDEODEV_RC_OK)
        return rc;

    vd->is_streaming = true;
    return VIDEODEV_RC_OK;
}

int qemu_videodev_stream_off(Videodev *vd, Error **errp)
{
    VideodevClass *vc = VIDEODEV_GET_CLASS(vd);
    int rc;

    if (vd->is_streaming == false) {

        vd_error_setg(vd, errp, "could not disable streaming. Already disabled!");
        return VIDEODEV_RC_ERROR;
    }

    if (vc->stream_off == NULL) {

        vd_error_setg(vd, errp, "missing 'stream_off' method!");
        return VIDEODEV_RC_NOTSUP;
    }

    if (videodev_frame_ready(vd) == true)
        videodev_release_frame(vd, NULL);

    if ((rc = vc->stream_off(vd, errp)) != VIDEODEV_RC_OK)
        return rc;

    vd->is_streaming = false;
    return VIDEODEV_RC_OK;
}

int qemu_videodev_read_frame(Videodev *vd, const size_t upto, VideoFrameChunk *chunk, Error **errp)
{
    int rc;

    if (videodev_frame_ready(vd) == false) {

        if ((rc = videodev_claim_frame(vd, errp)) != VIDEODEV_RC_OK) {
            return rc;
        }
    }

    chunk->size = MIN(vd->current_frame.bytes_left, upto);
    chunk->data = vd->current_frame.data;

    vd->current_frame.data        = vd->current_frame.data + chunk->size;
    vd->current_frame.bytes_left -= chunk->size;

    return VIDEODEV_RC_OK;
}

int qemu_videodev_read_frame_done(Videodev *vd, Error **errp)
{
    int rc;

    if (vd->current_frame.bytes_left == 0) {

        if ((rc = videodev_release_frame(vd, errp)) != VIDEODEV_RC_OK) {
            return rc;
        }
    }

    return VIDEODEV_RC_OK;
}

size_t qemu_videodev_current_frame_length(Videodev *vd) {

    return vd->current_frame.bytes_left;
}

VideoFramerate *qemu_videodev_get_framerates(Videodev *vd, uint8_t format_idx, uint8_t frame_idx, int *n)
{
    *n   = vd->modes[format_idx].framesizes[frame_idx].nframerate;
    return vd->modes[format_idx].framesizes[frame_idx].framerates;
}

static void video_instance_init(Object *obj) {

    Videodev *vd = VIDEODEV(obj);

    vd->registered   = false;
    vd->is_streaming = false;
}

static const TypeInfo video_type_info = {
    .name = TYPE_VIDEODEV,
    .parent = TYPE_OBJECT,
    .instance_init = video_instance_init,
    .instance_size = sizeof(Videodev),
    .abstract = true,
    .class_size = sizeof(VideodevClass),
};

static void register_types(void) {

    type_register_static(&video_type_info);
}

type_init(register_types);
