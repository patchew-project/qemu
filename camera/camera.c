/*
 * QEMU camera subsystem
 *
 * Copyright 2021 Bytedance, Inc.
 *
 * Authors:
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/help_option.h"
#include "qemu/iov.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/qemu-print.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-camera.h"
#include "camera/camera.h"
#include "camera-int.h"
#include "trace.h"

static QLIST_HEAD(, QEMUCamera) qemu_cameras;

const char *qemu_camera_id(QEMUCamera *camera)
{
    if (camera->dev && camera->dev->id) {
        return camera->dev->id;
    }

    return "";
}

QEMUCamera *qemu_camera_by_id(const char *id)
{
    QEMUCamera *camera;

    if (!id) {
        return NULL;
    }

    QLIST_FOREACH(camera, &qemu_cameras, list) {
        if (!strcmp(qemu_camera_id(camera), id)) {
            return camera;
        }
    }

    return NULL;
}

static const QEMUCameraClass *camera_get_class(const char *typename,
                                               Error **errp)
{
    ObjectClass *oc;

    oc = module_object_class_by_name(typename);

    if (!object_class_dynamic_cast(oc, TYPE_CAMERADEV)) {
        error_setg(errp, "%s: missing %s implementation",
                   TYPE_CAMERADEV, typename);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, "%s: %s is abstract type", TYPE_CAMERADEV, typename);
        return NULL;
    }

    return CAMERADEV_CLASS(oc);
}

static QEMUCamera *qemu_camera_new(Cameradev *dev, Error **errp)
{
    Object *obj;
    QEMUCamera *camera = NULL;
    g_autofree char *typename = NULL;
    Error *local_err = NULL;
    QEMUCameraClass *klass;
    const char *driver = CameradevDriver_str(dev->driver);

    typename = g_strdup_printf("%s-%s", TYPE_CAMERADEV, driver);
    if (!camera_get_class(typename, errp)) {
        return NULL;
    }

    obj = object_new(typename);
    if (!obj) {
        return NULL;
    }

    camera = CAMERADEV(obj);
    camera->dev = dev;

    klass = CAMERADEV_GET_CLASS(camera);
    if (klass->open) {
        klass->open(camera, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto error;
        }
    }

    QLIST_INSERT_HEAD(&qemu_cameras, camera, list);
    trace_qemu_camera_new(qemu_camera_id(camera), typename);

    return camera;

error:
    if (obj) {
        object_unref(obj);
    }

    return NULL;
}

typedef struct CameradevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} CameradevClassFE;

static void cameradev_class_foreach(ObjectClass *klass, void *opaque)
{
    CameradevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), TYPE_CAMERADEV"-"));
    fe->fn(object_class_get_name(klass) + 10, fe->opaque);
}

static void cameradev_name_foreach(void (*fn)(const char *name, void *opaque),
                                   void *opaque)
{
    CameradevClassFE fe = { .fn = fn, .opaque = opaque };

    object_class_foreach(cameradev_class_foreach, TYPE_CAMERADEV, false, &fe);
}

static void help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;

    g_string_append_printf(str, "\n  %s", name);
}

void qemu_camera_new_from_opts(const char *opt)
{
    Cameradev *dev;

    if (opt && is_help_option(opt)) {
        GString *str = g_string_new("");

        cameradev_name_foreach(help_string_append, str);

        qemu_printf("Available cameradev backend types: %s\n", str->str);
        g_string_free(str, true);
        return;
    }

    Visitor *v = qobject_input_visitor_new_str(opt, "driver", &error_fatal);
    visit_type_Cameradev(v, NULL, &dev, &error_fatal);
    visit_free(v);

    if (qemu_camera_by_id(dev->id)) {
        error_setg(&error_fatal, "%s: id %s already existed",
                   TYPE_CAMERADEV, dev->id);
    }

    if (!qemu_camera_new(dev, &error_fatal)) {
        qapi_free_Cameradev(dev);
    }
}

void qemu_camera_del(QEMUCamera *camera)
{
    Error *local_err = NULL;

    trace_qemu_camera_del(qemu_camera_id(camera));

    qemu_camera_stream_off(camera, &local_err);
    QLIST_REMOVE(camera, list);
    qapi_free_Cameradev(camera->dev);
    object_unref(camera);
}

int qemu_camera_enum_pixel_format(QEMUCamera *camera, uint32_t *pixfmts,
                                  int npixfmt, Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);
    int ret, i;

    if (!klass->enum_pixel_format) {
        error_setg(errp, "%s: %s missing enum pixel format implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return -ENOTSUP;
    }

    ret = klass->enum_pixel_format(camera, pixfmts, npixfmt, errp);
    if (ret > 0) {
        for (i = 0; i < ret; i++) {
            trace_qemu_camera_enum_pixel_format(qemu_camera_id(camera),
                pixfmts[i]);
        }
    } else {
        trace_qemu_camera_enum_pixel_format_ret(qemu_camera_id(camera), ret);
    }

    return ret;
}

int qemu_camera_enum_frame_size(QEMUCamera *camera, uint32_t pixfmt,
                                QEMUCameraFrameSize *frmszs, int nfrmsz,
                                Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);
    int ret, i;

    if (!klass->enum_frame_size) {
        error_setg(errp, "%s: %s missing enum frame size implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return -ENOTSUP;
    }

    ret = klass->enum_frame_size(camera, pixfmt, frmszs, nfrmsz, errp);
    if (ret > 0) {
        QEMUCameraFrameSize *frmsz;

        for (i = 0; i < ret; i++) {
            frmsz = frmszs + i;
            if (frmsz->type == QEMU_CAMERA_FRMSIZE_TYPE_DISCRETE)
                trace_qemu_camera_enum_frame_size_d(qemu_camera_id(camera),
                    frmsz->pixel_format, frmsz->d.width, frmsz->d.height);
            }
    } else {
        trace_qemu_camera_enum_frame_size_ret(qemu_camera_id(camera), ret);
    }

    return ret;
}

int qemu_camera_enum_frame_interval(QEMUCamera *camera,
                                    const QEMUCameraFormat *format,
                                    QEMUCameraFrameInterval *frmivals,
                                    int nfrmival, Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);
    int ret, i;

    if (!klass->enum_frame_interval) {
        error_setg(errp, "%s: %s missing enum frame interval implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return -ENOTSUP;
    }

    ret = klass->enum_frame_interval(camera, format, frmivals, nfrmival, errp);
    if (ret > 0) {
        QEMUCameraFrameInterval *frmival;

        for (i = 0; i < ret; i++) {
            frmival = frmivals + i;
            if (frmival->type == QEMU_CAMERA_FRMIVAL_TYPE_DISCRETE) {
                trace_qemu_camera_enum_frame_interval_d(qemu_camera_id(camera),
                    frmival->pixel_format, frmival->width, frmival->height,
                    frmival->d.numerator, frmival->d.denominator);
            }
        }
    } else {
        trace_qemu_camera_enum_frame_interval_ret(qemu_camera_id(camera), ret);
    }

    return ret;
}

int qemu_camera_set_frame_interval(QEMUCamera *camera,
                                   const QEMUCameraFrameInterval *frmival,
                                   Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);
    int ret;

    if (!klass->set_frame_interval) {
        error_setg(errp, "%s: %s missing set frame interval implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return -ENOTSUP;
    }

    ret = klass->set_frame_interval(camera, frmival, errp);
    if (frmival->type == QEMU_CAMERA_FRMIVAL_TYPE_DISCRETE) {
        trace_qemu_camera_set_frame_interval(qemu_camera_id(camera),
            frmival->pixel_format, frmival->width, frmival->height,
            frmival->d.numerator, frmival->d.denominator, ret);
    }

    return ret;
}

int qemu_camera_enum_control(QEMUCamera *camera, QEMUCameraControl *controls,
                             int ncontrols, Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);
    int ret, i;

    if (!klass->enum_control) {
        error_setg(errp, "%s: %s missing enum control implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return -ENOTSUP;
    }

    ret = klass->enum_control(camera, controls, ncontrols, errp);
    if (ret > 0) {
        QEMUCameraControl *control;

        for (i = 0; i < ret; i++) {
            control = controls + i;
            trace_qemu_camera_enum_control(qemu_camera_id(camera),
                QEMUCameraControlTypeString(control->type), control->def,
                control->min, control->max, control->step);
        }
    } else {
        trace_qemu_camera_enum_control_ret(qemu_camera_id(camera), ret);
    }

    return ret;
}

int qemu_camera_set_control(QEMUCamera *camera,
                            const QEMUCameraControl *control, Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);

    if (!klass->set_control) {
        error_setg(errp, "%s: %s missing set control implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return -ENOTSUP;
    }

    trace_qemu_camera_set_control(qemu_camera_id(camera),
        QEMUCameraControlTypeString(control->type), control->cur);

    return klass->set_control(camera, control, errp);
}

void qemu_camera_stream_on(QEMUCamera *camera, qemu_camera_image_cb cb,
                           void *opaque, Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);

    if (!klass->stream_on) {
        error_setg(errp, "%s: %s missing stream on implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return;
    }

    qemu_mutex_lock(&camera->image_lock);
    camera->cb_fn = cb;
    camera->cb_opaque = opaque;
    qemu_mutex_unlock(&camera->image_lock);

    klass->stream_on(camera, errp);
    assert(camera->image_addr);
    assert(camera->image_size);

    trace_qemu_camera_stream_on(qemu_camera_id(camera));
}

void qemu_camera_stream_off(QEMUCamera *camera, Error **errp)
{
    QEMUCameraClass *klass = CAMERADEV_GET_CLASS(camera);

    if (!klass->stream_off) {
        error_setg(errp, "%s: %s missing stream off implementation",
                   TYPE_CAMERADEV, qemu_camera_id(camera));
        return;
    }

    qemu_mutex_lock(&camera->image_lock);
    camera->cb_fn = NULL;
    camera->cb_opaque = NULL;
    qemu_mutex_unlock(&camera->image_lock);

    klass->stream_off(camera, errp);

    trace_qemu_camera_stream_off(qemu_camera_id(camera));
}

size_t qemu_camera_stream_length(QEMUCamera *camera)
{
    size_t length = 0;

    qemu_mutex_lock(&camera->image_lock);
    assert(camera->image_pos <= camera->image_bytes);
    length = camera->image_bytes - camera->image_pos;
    qemu_mutex_unlock(&camera->image_lock);

    return length;
}

size_t qemu_camera_stream_read(QEMUCamera *camera, const struct iovec *iov,
                               const uint32_t iov_cnt, size_t offset,
                               size_t size)
{
    size_t length = 0;
    void *addr;

    qemu_mutex_lock(&camera->image_lock);

    assert(camera->image_pos <= camera->image_bytes);
    length = camera->image_bytes - camera->image_pos;
    length = MIN(size, length);
    if (!length) {
        goto out;
    }

    addr = camera->image_addr + camera->image_pos;
    iov_from_buf(iov, iov_cnt, offset, addr, size);
    camera->image_pos += length;
    if (camera->image_pos == camera->image_bytes) {
        /* previous frame already fully read*/
        camera->image_frames = camera->image_sequence;
        camera->image_pos = 0;
        camera->image_bytes = 0;
    }

out:
    qemu_mutex_unlock(&camera->image_lock);

    trace_qemu_camera_stream_read(qemu_camera_id(camera), length);

    return length;
}


static void camera_init(Object *obj)
{
    QEMUCamera *camera = CAMERADEV(obj);

    qemu_mutex_init(&camera->image_lock);
}

static void camera_finalize(Object *obj)
{
    QEMUCamera *camera = CAMERADEV(obj);

    qemu_mutex_destroy(&camera->image_lock);
}

static const TypeInfo camera_type_info = {
    .name = TYPE_CAMERADEV,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(QEMUCamera),
    .instance_init = camera_init,
    .instance_finalize = camera_finalize,
    .abstract = true,
    .class_size = sizeof(QEMUCameraClass),
};

static void register_types(void)
{
    type_register_static(&camera_type_info);
}

type_init(register_types);

/* internal functions, declared in camera-int.h */

void qemu_camera_alloc_image(QEMUCamera *camera, size_t size, Error **errp)
{
    trace_qemu_camera_alloc_image(qemu_camera_id(camera), size);

    qemu_mutex_lock(&camera->image_lock);
    if (camera->image_size == size) {
        /* no need to re-allocate the same size image buffer */
        goto out;
    }

    g_free(camera->image_addr);
    camera->image_addr = g_malloc0(size);
    camera->image_size = size;
    camera->image_pos = 0;
    camera->image_bytes = 0;

out:
    qemu_mutex_unlock(&camera->image_lock);
}

void qemu_camera_free_image(QEMUCamera *camera)
{
    trace_qemu_camera_free_image(qemu_camera_id(camera));

    qemu_mutex_lock(&camera->image_lock);
    g_free(camera->image_addr);
    camera->image_addr = NULL;
    camera->image_size = 0;
    camera->image_pos = 0;
    camera->image_bytes = 0;
    qemu_mutex_unlock(&camera->image_lock);
}

void qemu_camera_new_image(QEMUCamera *camera, const void *addr, size_t size)
{
    trace_qemu_camera_new_image(qemu_camera_id(camera), camera->image_sequence,
                                size);
    qemu_mutex_lock(&camera->image_lock);

    assert(camera->image_addr);
    assert(size <= camera->image_size);

    camera->image_sequence++;

    if (camera->image_pos) {
        /* previous frame in process */
        goto out;
    }

    memcpy(camera->image_addr, addr, size);
    camera->image_bytes = size;

out:
    qemu_mutex_unlock(&camera->image_lock);
}
