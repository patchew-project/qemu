/*
 * V4L2 camera backend implemention
 *
 * Copyright 2021-2022 Bytedance, Inc.
 *
 * Authors:
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qapi-visit-camera.h"
#include "camera/camera.h"
#include "camera-int.h"
#include "trace.h"
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#define TYPE_CAMERA_V4L2 TYPE_CAMERADEV"-v4l2"

#define CAMERA_V4L2_BUFFER_MAX 16
#define CAMERA_V4L2_BUFFER_DEF 2

typedef struct CameraV4l2Buffer {
    unsigned char *addr;
    uint32_t length;
} CameraV4l2Buffer;

typedef struct CameraV4l2 {
    QEMUCamera parent;

    int devfd;
    size_t sizeimage;
    uint8_t nbuffers;
    CameraV4l2Buffer buffers[CAMERA_V4L2_BUFFER_MAX];
} CameraV4l2;

DECLARE_INSTANCE_CHECKER(CameraV4l2, CAMERA_V4L2_DEV, TYPE_CAMERA_V4L2)

typedef struct CameraV4l2Ctrl {
    QEMUCameraControlType q;
    uint32_t v;
} CameraV4l2Ctrl;

static CameraV4l2Ctrl camera_v4l2_ctrl_table[] = {
    { .q = QEMUCameraBrightness,
      .v = V4L2_CID_BRIGHTNESS },
    { .q = QEMUCameraContrast,
      .v = V4L2_CID_CONTRAST },
    { .q = QEMUCameraGain,
      .v = V4L2_CID_GAIN },
    { .q = QEMUCameraGamma,
      .v = V4L2_CID_GAMMA },
    { .q = QEMUCameraHue,
      .v = V4L2_CID_HUE },
    { .q = QEMUCameraHueAuto,
      .v = V4L2_CID_HUE_AUTO },
    { .q = QEMUCameraSaturation,
      .v = V4L2_CID_SATURATION },
    { .q = QEMUCameraSharpness,
      .v = V4L2_CID_SHARPNESS },
    { .q = QEMUCameraWhiteBalanceTemperature,
      .v = V4L2_CID_WHITE_BALANCE_TEMPERATURE },
};

static QEMUCameraControlType camera_v4l2_control_to_qemu(uint32_t id)
{
    CameraV4l2Ctrl *ctrl;
    int i;

    for (i = 0; i < ARRAY_SIZE(camera_v4l2_ctrl_table); i++) {
        ctrl = &camera_v4l2_ctrl_table[i];
        if (ctrl->v == id) {
            return ctrl->q;
        }
    }

    return QEMUCameraControlMax;
}

static uint32_t camera_qemu_control_to_v4l2(QEMUCameraControlType type)
{
    CameraV4l2Ctrl *ctrl;
    int i;

    for (i = 0; i < ARRAY_SIZE(camera_v4l2_ctrl_table); i++) {
        ctrl = &camera_v4l2_ctrl_table[i];
        if (ctrl->q == type) {
            return ctrl->v;
        }
    }

    return 0;
}

static int camera_v4l2_enum_pixel_format(QEMUCamera *camera, uint32_t *pixfmts,
                                         int npixfmt, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    struct v4l2_fmtdesc v4l2_fmt;
    int index, total = 0;

    for (index = 0; total < npixfmt; index++) {
        v4l2_fmt.index = index;
        v4l2_fmt.type = V4L2_CAP_VIDEO_CAPTURE;
        if (ioctl(v4l2->devfd, VIDIOC_ENUM_FMT, &v4l2_fmt) < 0) {
            if (errno == EINVAL) {
                break; /* the last one */
            }

            error_setg(errp, "%s: enum fmt on device %s failed, %s",
                       TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
            return -errno;
        }

        if (!qemu_camera_pixel_supported(v4l2_fmt.pixelformat)) {
            continue;
        }

        pixfmts[total++] = v4l2_fmt.pixelformat;
    }

    return total;
}

static int camera_v4l2_enum_frame_size(QEMUCamera *camera, uint32_t pixfmt,
                                       QEMUCameraFrameSize *frmszs, int nfrmsz,
                                       Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    QEMUCameraFrameSize *frmsz;
    struct v4l2_frmsizeenum v4l2_frmsz;
    int index, total = 0;

    if (!qemu_camera_pixel_supported(pixfmt)) {
        return -EINVAL;
    }

    for (index = 0; total < nfrmsz; index++) {
        v4l2_frmsz.index = index;
        v4l2_frmsz.pixel_format = pixfmt;
        if (ioctl(v4l2->devfd, VIDIOC_ENUM_FRAMESIZES, &v4l2_frmsz) < 0) {
            if (errno == EINVAL) {
                break; /* the last one */
            }

            error_setg(errp, "%s: enum frame size device %s failed, %s",
                       TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
            return -errno;
        }

        if (v4l2_frmsz.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
            continue; /* TODO stepwise support in future*/
        }

        frmsz = frmszs + total++;
        frmsz->pixel_format = v4l2_frmsz.pixel_format;
        frmsz->type = QEMU_CAMERA_FRMSIZE_TYPE_DISCRETE;
        frmsz->d.width = v4l2_frmsz.discrete.width;
        frmsz->d.height = v4l2_frmsz.discrete.height;
    }

    return total;
}

static int camera_v4l2_enum_frame_interval(QEMUCamera *camera,
                                           const QEMUCameraFormat *format,
                                           QEMUCameraFrameInterval *frmivals,
                                           int nfrmival, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    QEMUCameraFrameInterval *frmival;
    struct v4l2_frmivalenum v4l2_frmival;
    int index, total = 0;

    for (index = 0; total < nfrmival; index++) {
        v4l2_frmival.index = index;
        v4l2_frmival.pixel_format = format->pixel_format;
        v4l2_frmival.width = format->width;
        v4l2_frmival.height = format->height;
        if (ioctl(v4l2->devfd, VIDIOC_ENUM_FRAMEINTERVALS, &v4l2_frmival) < 0) {
            if (errno == EINVAL) {
                break; /* the last one */
            }

            error_setg(errp, "%s: enum frame intervals on device %s failed, %s",
                       TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
            return -errno;
        }

        if (v4l2_frmival.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
            continue; /* TODO stepwise support in future*/
        }

        frmival = frmivals + total++;
        frmival->pixel_format = v4l2_frmival.pixel_format;
        frmival->type = QEMU_CAMERA_FRMIVAL_TYPE_DISCRETE;
        frmival->width = v4l2_frmival.width;
        frmival->height = v4l2_frmival.height;
        frmival->d.numerator = v4l2_frmival.discrete.numerator;
        frmival->d.denominator = v4l2_frmival.discrete.denominator;
    }

    return total;
}

static int camera_v4l2_get_format(QEMUCamera *camera, QEMUCameraFormat *format,
                                  Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    struct v4l2_format v4l2_fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    struct v4l2_pix_format *v4l2_pix = &v4l2_fmt.fmt.pix;

    if (ioctl(v4l2->devfd, VIDIOC_G_FMT, &v4l2_fmt) < 0) {
        error_setg(errp, "%s: get fmt on device %s failed, %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
        return -errno;
    }

    if (format) {
        format->pixel_format = v4l2_pix->pixelformat;
        format->width = v4l2_pix->width;
        format->height = v4l2_pix->height;
    }

    v4l2->sizeimage = v4l2_pix->sizeimage;

    return 0;
}

static int camera_v4l2_set_format(QEMUCamera *camera,
                                  const QEMUCameraFormat *format, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    struct v4l2_format v4l2_fmt = {0};
    struct v4l2_pix_format *v4l2_pix;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_pix = &v4l2_fmt.fmt.pix;
    v4l2_pix->pixelformat = format->pixel_format;
    v4l2_pix->width = format->width;
    v4l2_pix->height = format->height;
    if (ioctl(v4l2->devfd, VIDIOC_S_FMT, &v4l2_fmt) < 0) {
        error_setg(errp, "%s: set fmt on device %s failed, %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
        return -errno;
    }

    v4l2->sizeimage = v4l2_pix->sizeimage;

    return 0;
}

static int camera_v4l2_set_frame_interval(QEMUCamera *camera,
               const QEMUCameraFrameInterval *frmival, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    QEMUCameraFormat fmt;
    struct v4l2_streamparm streamparm;
    struct v4l2_captureparm *capture;
    int ret;

    if (frmival->type != QEMU_CAMERA_FRMIVAL_TYPE_DISCRETE) {
        error_setg(errp, "%s: only support discrete mode", TYPE_CAMERA_V4L2);
        return -ENOTSUP;
    }

    fmt.pixel_format = frmival->pixel_format;
    fmt.width = frmival->width;
    fmt.height = frmival->height;
    ret = camera_v4l2_set_format(camera, &fmt, errp);
    if (ret) {
        return ret;
    }

    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    capture = &streamparm.parm.capture;
    capture->timeperframe.numerator = frmival->d.numerator;
    capture->timeperframe.denominator = frmival->d.denominator;
    if (ioctl(v4l2->devfd, VIDIOC_S_PARM, &streamparm) < 0) {
        error_setg(errp, "%s: set stream parm on device %s failed, %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
        return -errno;
    }

    return 0;
}

static int camera_v4l2_enum_control(QEMUCamera *camera,
               QEMUCameraControl *controls, int ncontrols, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    QEMUCameraControl *control;
    struct v4l2_queryctrl v4l2_ctrl = {0};
    QEMUCameraControlType type;
    int index, total = 0;

    for (index = 0; total < ncontrols; index++) {
        v4l2_ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        if (ioctl(v4l2->devfd, VIDIOC_QUERYCTRL, &v4l2_ctrl) < 0) {
            if (errno == EINVAL) {
                break; /* the last one */
            }

            error_setg(errp, "%s: enum control on device %s failed, %s",
                       TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
            return -errno;
        }

        if (v4l2_ctrl.flags & V4L2_CTRL_FLAG_INACTIVE) {
            continue;
        }

        type = camera_v4l2_control_to_qemu(v4l2_ctrl.id);
        if (type == QEMUCameraControlMax) {
            continue;
        }

        control = controls + total++;
        control->type = type;
        control->def = v4l2_ctrl.default_value;
        control->min = v4l2_ctrl.minimum;
        control->max = v4l2_ctrl.maximum;
        control->step = v4l2_ctrl.step;
    }

    return total;
}

static int camera_v4l2_set_control(QEMUCamera *camera,
               const QEMUCameraControl *control, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    struct v4l2_control v4l2_ctrl;
    uint32_t cid;

    cid = camera_qemu_control_to_v4l2(control->type);
    if (!cid) {
        error_setg(errp, "%s: unsupported control type %d",
                   TYPE_CAMERA_V4L2, control->type);
        return -EINVAL;
    }

    v4l2_ctrl.id = cid;
    v4l2_ctrl.value = control->cur;
    if (ioctl(v4l2->devfd, VIDIOC_S_CTRL, &v4l2_ctrl) < 0) {
        error_setg(errp, "%s: set ctrl on device %s failed, %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
        return -errno;
    }

    return 0;
}

static int camera_v4l2_qbuf(QEMUCamera *camera, int index)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    struct v4l2_buffer buffer = {0};

    trace_camera_v4l2_qbuf(qemu_camera_id(camera), index);

    buffer.index = index;
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.field = V4L2_FIELD_ANY;
    buffer.memory = V4L2_MEMORY_MMAP;

    return ioctl(v4l2->devfd, VIDIOC_QBUF, &buffer);
}

static int camera_v4l2_dqbuf(QEMUCamera *camera)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    struct v4l2_buffer buffer;

    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(v4l2->devfd, VIDIOC_DQBUF, &buffer) < 0) {
        return -errno;
    }

    trace_camera_v4l2_dqbuf(qemu_camera_id(camera), buffer.index);

    return buffer.index;
}

static void camera_v4l2_free_buffers(QEMUCamera *camera)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    struct v4l2_requestbuffers v4l2_reqbufs = {0};
    CameraV4l2Buffer *buffer;
    int index;

    /* 1, try to dequeue all buffers */
    for (index = 0; index < v4l2->nbuffers; index++) {
        camera_v4l2_dqbuf(camera);
    }

    /* 2, try to unmap all buffers */
    for (index = 0; index < v4l2->nbuffers; index++) {
        buffer = &v4l2->buffers[index];
        if (buffer->addr) {
            munmap(buffer->addr, buffer->length);
            buffer->addr = NULL;
            buffer->length = 0;
        }
    }

    /* 3, free all the v4l2 reqbufs */
    v4l2_reqbufs.count = 0;
    v4l2_reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_reqbufs.memory = V4L2_MEMORY_MMAP;
    ioctl(v4l2->devfd, VIDIOC_REQBUFS, &v4l2_reqbufs);
}

static int camera_v4l2_request_buffers(QEMUCamera *camera, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    struct v4l2_requestbuffers v4l2_reqbufs = {0};
    struct v4l2_buffer v4l2_buf;
    CameraV4l2Buffer *buffer;
    void *addr;
    int index;

    v4l2_reqbufs.count = v4l2->nbuffers;
    v4l2_reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_reqbufs.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2->devfd, VIDIOC_REQBUFS, &v4l2_reqbufs) < 0) {
        return -errno;
    }

    for (index = 0; index < v4l2->nbuffers; index++) {
        v4l2_buf.index = index;
        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_MMAP;
        v4l2_buf.length = 0;
        if (ioctl(v4l2->devfd, VIDIOC_QUERYBUF, &v4l2_buf) < 0) {
            goto error;
        }

        if (v4l2_buf.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
            continue; /* TODO V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE support */
        }

        addr = mmap(NULL, v4l2_buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                    v4l2->devfd, v4l2_buf.m.offset);
        if (addr == MAP_FAILED) {
            goto error;
        }

        if (camera_v4l2_qbuf(camera, index) < 0) {
            goto error;
        }

        buffer = &v4l2->buffers[index];
        buffer->addr = addr;
        buffer->length = v4l2_buf.length;
    }

    return 0;

error:
    camera_v4l2_free_buffers(camera);

    return -errno;
}

static void camera_v4l2_open(QEMUCamera *camera, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    struct v4l2_capability v4l2_cap = {0};

    if (v4l2opts->has_buffers) {
        if (!v4l2opts->buffers || v4l2opts->buffers > CAMERA_V4L2_BUFFER_MAX) {
            error_setg(errp, "%s: zero buffers or too large(max %d)",
                       TYPE_CAMERA_V4L2, CAMERA_V4L2_BUFFER_MAX);
            return;
        }

        v4l2->nbuffers = v4l2opts->buffers;
    } else {
        v4l2->nbuffers = CAMERA_V4L2_BUFFER_DEF;
    }

    if (!v4l2opts->has_path) {
        error_setg(errp, "%s: missing device path", TYPE_CAMERA_V4L2);
        return;
    }

    v4l2->devfd = open(v4l2opts->path, O_RDWR | O_NONBLOCK);
    if (v4l2->devfd == -1) {
        error_setg(errp, "%s: open device %s failed, %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
        return;
    }

    if (ioctl(v4l2->devfd, VIDIOC_QUERYCAP, &v4l2_cap) < 0) {
        error_setg(errp, "%s: query device %s failed, %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path, strerror(errno));
        goto error;
    }

    if (!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(v4l2_cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        error_setg(errp, "%s: %s is not a video capture device",
                   TYPE_CAMERA_V4L2, v4l2opts->path);
        goto error;
    }

    if (camera_v4l2_get_format(camera, NULL, errp) < 0) {
        goto error;
    }

    return;

error:
    if (v4l2->devfd > 0) {
        close(v4l2->devfd);
    }
}

static void camera_v4l2_read_handler(void *opaque)
{
    QEMUCamera *camera = (QEMUCamera *)opaque;
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Buffer *buffer;
    int index;

    index = camera_v4l2_dqbuf(camera);
    if (index < 0) {
        return;
    }

    buffer = &v4l2->buffers[index];
    qemu_camera_new_image(camera, buffer->addr, buffer->length);

    camera_v4l2_qbuf(camera, index);
}

static void camera_v4l2_stream_on(QEMUCamera *camera, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    qemu_camera_alloc_image(camera, v4l2->sizeimage, errp);

    if (camera_v4l2_request_buffers(camera, errp)) {
        return;
    }

    if (ioctl(v4l2->devfd, VIDIOC_STREAMON, &type) < 0) {
        error_setg(errp, "%s: stream on failed on %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path);
        camera_v4l2_free_buffers(camera);
        return;
    }

    qemu_set_fd_handler(v4l2->devfd, camera_v4l2_read_handler, NULL, camera);
}

static void camera_v4l2_stream_off(QEMUCamera *camera, Error **errp)
{
    CameraV4l2 *v4l2 = CAMERA_V4L2_DEV(camera);
    CameraV4l2Options *v4l2opts = &camera->dev->u.v4l2;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    qemu_set_fd_handler(v4l2->devfd, NULL, NULL, camera);

    if (ioctl(v4l2->devfd, VIDIOC_STREAMOFF, &type) < 0) {
        error_setg(errp, "%s: stream off failed on %s",
                   TYPE_CAMERA_V4L2, v4l2opts->path);
    }

    camera_v4l2_free_buffers(camera);

    qemu_camera_free_image(camera);
}

static void camera_v4l2_init(Object *obj)
{
}

static void camera_v4l2_finalize(Object *obj)
{
    QEMUCamera *camera = CAMERADEV(obj);
    Error *local_err = NULL;

    camera_v4l2_stream_off(camera, &local_err);
}

static void camera_v4l2_class_init(ObjectClass *oc, void *data)
{
    QEMUCameraClass *klass = CAMERADEV_CLASS(oc);

    klass->open = camera_v4l2_open;
    klass->stream_on = camera_v4l2_stream_on;
    klass->stream_off = camera_v4l2_stream_off;
    klass->enum_pixel_format = camera_v4l2_enum_pixel_format;
    klass->enum_frame_size = camera_v4l2_enum_frame_size;
    klass->enum_frame_interval = camera_v4l2_enum_frame_interval;
    klass->set_frame_interval = camera_v4l2_set_frame_interval;
    klass->enum_control = camera_v4l2_enum_control;
    klass->set_control = camera_v4l2_set_control;
}

static const TypeInfo camera_v4l2_type_info = {
    .name = TYPE_CAMERA_V4L2,
    .parent = TYPE_CAMERADEV,
    .instance_size = sizeof(CameraV4l2),
    .instance_init = camera_v4l2_init,
    .instance_finalize = camera_v4l2_finalize,
    .class_init = camera_v4l2_class_init,
};

static void register_types(void)
{
    type_register_static(&camera_v4l2_type_info);
}

type_init(register_types);
