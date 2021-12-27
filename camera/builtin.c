/*
 * Builtin camera backend implemention
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

#ifdef CONFIG_VNC_JPEG /* TODO shoud it rename to CONFIG_LIB_JPEG? */
#include <jpeglib.h>
#endif

#include <cairo/cairo.h>

#define TYPE_CAMERA_BUILTIN TYPE_CAMERADEV"-builtin"

#define CAMERA_BUILTIN_DEF_WIDTH 640
#define CAMERA_BUILTIN_MAX_WIDTH 3840
#define CAMERA_BUILTIN_MIN_WIDTH 160
#define CAMERA_BUILTIN_DEF_HEIGHT 480
#define CAMERA_BUILTIN_MAX_HEIGHT 2880
#define CAMERA_BUILTIN_MIN_HEIGHT 120
#define CAMERA_BUILTIN_DEF_FPS 10
#define CAMERA_BUILTIN_MAX_FPS 60
#define CAMERA_BUILTIN_MIN_FPS 1

/* mjpeg, yuv, rgb565 */
#define CAMERA_BUILTIN_MAX_PIXFMT 3

enum AttributeIndex {
    ATTRIBUTE_DEF,
    ATTRIBUTE_MIN,
    ATTRIBUTE_MAX,
    ATTRIBUTE_CUR,
    ATTRIBUTE_STEP,

    ATTRIBUTE_ALL
};

typedef struct CameraBuiltin {
    QEMUCamera parent;

    /* opts */
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    bool debug;
    bool mjpeg;
    bool yuv;
    bool rgb565;
    ColorType bgcolor;

    /* state */
    QEMUTimer *frame_timer;
    cairo_surface_t *surface;
    cairo_t *cr;
    size_t image_size;
    uint8_t *image;
    uint8_t pixbytes;
    uint8_t npixfmts;
    uint32_t pixfmts[CAMERA_BUILTIN_MAX_PIXFMT];
    uint32_t pixfmt; /* current in use */
    void *opaque; /* used by driver itself */

    /* debug infomations */
    uint32_t sequence;
    int32_t ctrl[QEMUCameraControlMax][ATTRIBUTE_ALL];
} CameraBuiltin;

DECLARE_INSTANCE_CHECKER(CameraBuiltin, CAMERA_BUILTIN_DEV, TYPE_CAMERA_BUILTIN)

static inline uint8_t pixel_clip(int x)
{
    if (x > 255) {
        return 255;
    } else if (x < 0) {
        return 0;
    }

    return x;
}

static void camera_builtin_rgb24_to_yuyv(uint8_t *rgb, uint8_t *yuv, int width,
                                         int height, uint8_t pixbytes)
{
    int r1, g1, b1, r2, g2, b2, y1, u1, y2, v1;
    int x, y;
    uint8_t *dst, *src;
    uint32_t val;

    for (x = 0; x < height; x++) {
        for (y = 0; y < width / 2; y++) {
            src = rgb + x * width * pixbytes + y * pixbytes * 2;
            val = le32_to_cpu(*(uint32_t *)src);
            r1 = (val >> 16) & 0xff;
            g1 = (val >> 8) & 0xff;
            b1 = val & 0xff;
            val = le32_to_cpu(*(uint32_t *)(src + pixbytes));
            r2 = (val >> 16) & 0xff;
            g2 = (val >> 8) & 0xff;
            b2 = val & 0xff;

            y1 = pixel_clip(((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16);
            u1 = pixel_clip((((-38 * r1 - 74 * g1 + 112 * b1 + 128) >> 8)
                            + ((-38 * r2 - 74 * g2 + 112 * b2 + 128) >> 8)) / 2
                            + 128);
            y2 = pixel_clip(((66 * r2 + 129 * g2 + 25 * b2 + 128) >> 8) + 16);
            v1 = pixel_clip((((112 * r1 - 94 * g1 - 18 * b1 + 128) >> 8)
                            + ((112 * r2 - 94 * g2 - 18 * b2 + 128) >> 8)) / 2
                            + 128);
            dst = yuv + x * width * 2 + y * 4;
            *dst++ = y1;
            *dst++ = u1;
            *dst++ = y2;
            *dst = v1;
        }
    }
}

static void camera_builtin_draw_info(QEMUCamera *camera)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    char text[32];
    uint16_t fontsize = 20;
    uint16_t y = fontsize;
    QEMUCameraControlType t;

    cairo_set_source_rgb(builtin->cr, 1, 1, 1);
    cairo_select_font_face(builtin->cr, "Georgia", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);

    cairo_set_font_size(builtin->cr, fontsize);
    cairo_move_to(builtin->cr, 0, y);
    sprintf(text, "Sequence %d", builtin->sequence++);
    cairo_show_text(builtin->cr, text);

    for (t = 0; (t < QEMUCameraControlMax)&&(y <= builtin->height); t++) {
        y += fontsize;
        cairo_move_to(builtin->cr, 0, y);
        sprintf(text, "%s %d", QEMUCameraControlTypeString(t),
                builtin->ctrl[t][ATTRIBUTE_CUR]);
        cairo_show_text(builtin->cr, text);
    }
}

static void camera_builtin_draw_pure_color(QEMUCamera *camera, ColorType color)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    uint8_t r = 0, g = 0, b = 0;

    switch ((int)color) {
    case COLOR_TYPE_RED:
        r = 1;
        break;
    case COLOR_TYPE_GREEN:
        g = 1;
        break;
    case COLOR_TYPE_BLUE:
        b = 1;
        break;
    }

    cairo_move_to(builtin->cr, 0, 0);
    cairo_set_source_rgb(builtin->cr, r, g, b);
    cairo_rectangle(builtin->cr, 0, 0, builtin->width, builtin->height);
    cairo_fill(builtin->cr);
}

static void camera_builtin_draw_rainbow(QEMUCamera *camera)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    uint16_t colors;
    uint16_t lines = builtin->height / 7;
    uint8_t rainbow[7][3] = {
        {0xff, 0x00, 0x00}, /* red */
        {0xff, 0xa5, 0x00}, /* orange */
        {0xff, 0xff, 0x00}, /* yellow */
        {0x00, 0x80, 0x00}, /* green */
        {0x00, 0x00, 0xff}, /* blue */
        {0x4b, 0x00, 0x82}, /* indigo */
        {0xee, 0x82, 0xee}, /* violet */
    };
    uint8_t *addr;

    for (colors = 0 ; colors < 7; colors++) {
        cairo_move_to(builtin->cr, 0, lines * colors);
        addr = rainbow[colors];
        cairo_set_source_rgb(builtin->cr, addr[0] / (float)255,
                             addr[1] / (float)255, addr[2] / (float)255);
        cairo_rectangle(builtin->cr, 0, lines * colors, builtin->width, lines);
        cairo_fill(builtin->cr);
    }
}

static void camera_builtin_draw_digital_rain(QEMUCamera *camera)
{
#define DIGITAL_RAIN_FONT 20
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    int rain_rows = builtin->width / 2 / DIGITAL_RAIN_FONT;
    int rain_len = builtin->height * 2 / DIGITAL_RAIN_FONT;
    int i, j, x, y, asterisks, first, last;
    char *addr, *base;
    char text[2] = {0};
    int len = rain_len / 2;

    if (!builtin->opaque) {
        builtin->opaque = g_malloc(rain_rows * rain_len);
        memset(builtin->opaque, '*', rain_rows * rain_len);
    }

    base = builtin->opaque;

    cairo_set_source_rgb(builtin->cr, 0, 0, 0);
    cairo_rectangle(builtin->cr, 0, 0, builtin->width, builtin->height);
    cairo_fill(builtin->cr);

    cairo_select_font_face(builtin->cr, "Georgia", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(builtin->cr, DIGITAL_RAIN_FONT);
    for (i = 0; i < rain_rows; i++) {
        addr = base + i * rain_len + len;
        asterisks = 0;

        for (j = 0; (j < len) && (addr[j] == '*'); j++) {
            asterisks++;
        }

        if (asterisks == len) {
rerandom:
            first = random() % len;
            last = random() % len;
            if ((first + len / 4) >= last) {
                goto rerandom;
            }

            for (j = first; j < last; j++) {
                *(addr + j) = random() % 26 + 'A' + (random() % 2) * 32;
            }
        }

        addr = base + i * rain_len;

        for (j = 0; (j < len) && (addr[j] == '*'); ) {
            j++;
        }

        if (j == len) {
            goto update_frame;
        }

        cairo_set_source_rgb(builtin->cr, 1, 1, 1); /* first char of row */
        x = DIGITAL_RAIN_FONT * i * 2;
        y = DIGITAL_RAIN_FONT * (len - j);
        cairo_move_to(builtin->cr, x, y);
        sprintf(text, "%c", addr[j]);
        cairo_show_text(builtin->cr, text);

        for (j++; j < len; j++) {
            if (addr[j] == '*') {
                continue;
            }
            x = DIGITAL_RAIN_FONT * i * 2;
            y = DIGITAL_RAIN_FONT * (len - j);
            cairo_set_source_rgb(builtin->cr, 0, 1, 0);
            cairo_move_to(builtin->cr, x, y);

            sprintf(text, "%c", addr[j]);
            cairo_show_text(builtin->cr, text);
        }

update_frame:
        addr = base + i * rain_len;
        memmove(addr, addr + 1, 2 * len - 1);
        addr[2 * len - 1] = '*';
    }
}

#ifdef CONFIG_VNC_JPEG
static void camera_builtin_jpeg_init_destination(j_compress_ptr cinfo)
{
    CameraBuiltin *builtin = cinfo->client_data;

    cinfo->dest->next_output_byte = builtin->image;
    cinfo->dest->free_in_buffer = builtin->image_size;
}

static void camera_builtin_jpeg_term_destination(j_compress_ptr cinfo)
{
    /* nothing to do, but avoid libjpeg to crash! */
}

static void camera_builtin_rgb24_to_jpeg_line(uint8_t *rgb, uint8_t *jpeg,
                                              uint16_t width, uint8_t pixbytes)
{
    uint16_t x;
    uint32_t val;
    uint8_t *dst, *src;

    for (x = 0; x < width; x++) {
        src = rgb + x * pixbytes;
        val = le32_to_cpu(*(uint32_t *)src);
        dst = jpeg + 3 * x;
        *(dst++) = (val >> 16) & 0xff; /* R */
        *(dst++) = (val >> 8) & 0xff; /* G */
        *dst = val & 0xff; /* B */
    }
}

static size_t camera_builtin_rgb24_to_jpeg(QEMUCamera *camera, uint8_t *rgb,
                                           uint8_t *jpeg, uint16_t width,
                                           int height, uint8_t pixbytes)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_destination_mgr manager = {0};
    JSAMPROW row_pointer[1];
    g_autofree uint8_t *linebuf = g_malloc(width * 3);
    uint8_t *addr;
    int quality = 50;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    cinfo.client_data = builtin;
    cinfo.image_width = builtin->width;
    cinfo.image_height = builtin->height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    manager.init_destination = camera_builtin_jpeg_init_destination;
    manager.term_destination = camera_builtin_jpeg_term_destination;
    cinfo.dest = &manager;
    row_pointer[0] = linebuf;

    jpeg_start_compress(&cinfo, true);

    while (cinfo.next_scanline < cinfo.image_height) {
        addr = rgb + cinfo.next_scanline * width * pixbytes;
        camera_builtin_rgb24_to_jpeg_line(addr, linebuf, width, pixbytes);
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return builtin->image_size - cinfo.dest->free_in_buffer;
}
#else
static size_t camera_builtin_rgb24_to_jpeg(QEMUCamera *camera, uint8_t *rgb,
                                           uint8_t *jpeg, uint16_t width,
                                           int height, uint8_t pixbytes)
{
    return -1;
}
#endif

static void camera_builtin_frame_timer(void *opaque)
{
    QEMUCamera *camera = opaque;
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    uint8_t *image_addr = cairo_image_surface_get_data(builtin->surface);
    size_t image_bytes = 0;
    uint16_t w = builtin->width, h = builtin->height;

    /* 1, draw a frame by cairo */
    switch (builtin->bgcolor) {
    case COLOR_TYPE_BLUE:
    case COLOR_TYPE_GREEN:
    case COLOR_TYPE_RED:
        camera_builtin_draw_pure_color(camera, builtin->bgcolor);
        break;

    case COLOR_TYPE_RAINBOW:
        camera_builtin_draw_rainbow(camera);
        break;

    case COLOR_TYPE_DIGITAL_RAIN:
        camera_builtin_draw_digital_rain(camera);
        break;

    case COLOR_TYPE__MAX:
    default:
        return;
    }

    if (builtin->debug) {
        camera_builtin_draw_info(camera);
    }

    /* 2, convert to a suitable format */
    switch (builtin->pixfmt) {
    case QEMU_CAMERA_PIX_FMT_MJPEG:
        image_bytes = camera_builtin_rgb24_to_jpeg(camera, image_addr,
                          builtin->image, w, h, builtin->pixbytes);
        image_addr = builtin->image;
        break;
    case QEMU_CAMERA_PIX_FMT_YUYV:
        camera_builtin_rgb24_to_yuyv(image_addr, builtin->image, w, h,
                                     builtin->pixbytes);
        image_bytes = w * h * 2;
        image_addr = builtin->image;
        break;
    case QEMU_CAMERA_PIX_FMT_RGB565:
        /* no need to convert, just put builtin->surface to uplayer */
        image_bytes = w * h * 2;
        break;
    }

    /* 3, delivery to uplayer */
    qemu_camera_new_image(camera, image_addr, image_bytes);

    /* 4, modify timer for next frame */
    timer_mod(builtin->frame_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
              + NANOSECONDS_PER_SECOND / builtin->fps);

    trace_camera_builtin_timer(qemu_camera_id(camera));
}

static int camera_builtin_enum_pixel_format(QEMUCamera *camera,
                                            uint32_t *pixfmts, int npixfmt,
                                            Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    int index, total = 0;

    for (index = 0; index < MIN(npixfmt, builtin->npixfmts); index++) {
        pixfmts[total++] = builtin->pixfmts[index];
    }

    return total;
}

static int camera_builtin_enum_frame_size(QEMUCamera *camera, uint32_t pixfmt,
                                          QEMUCameraFrameSize *frmszs,
                                          int nfrmsz, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    QEMUCameraFrameSize *frmsz;

    if (nfrmsz < 1) {
        return 0;
    }

    frmsz = frmszs;
    frmsz->pixel_format = pixfmt;
    frmsz->type = QEMU_CAMERA_FRMSIZE_TYPE_DISCRETE;
    frmsz->d.width = builtin->width;
    frmsz->d.height = builtin->height;

    return 1;
}

static int camera_builtin_enum_frame_interval(QEMUCamera *camera,
                                              const QEMUCameraFormat *format,
                                              QEMUCameraFrameInterval *frmivals,
                                              int nfrmival, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    QEMUCameraFrameInterval *frmival;

    if (nfrmival < 1) {
        return 0;
    }

    if (format->width != builtin->width || format->height != builtin->height) {
        error_setg(errp, "%s: enum frame interval unsupported mismatched "
                   "width(%d)/height(%d)", TYPE_CAMERA_BUILTIN, format->width,
                   format->height);
        return 0;
    }

    frmival = frmivals;
    frmival->pixel_format = format->pixel_format;
    frmival->width = format->width;
    frmival->height = format->height;
    frmival->type = QEMU_CAMERA_FRMIVAL_TYPE_DISCRETE;
    frmival->d.numerator = 1;
    frmival->d.denominator = builtin->fps;

    return 1;
}

static int camera_builtin_set_frame_interval(QEMUCamera *camera,
               const QEMUCameraFrameInterval *frmival, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);

    if (frmival->width != builtin->width
        || frmival->height != builtin->height) {
        error_setg(errp, "%s: set frame interval unsupported mismatched "
                   "width(%d)/height(%d)", TYPE_CAMERA_BUILTIN, frmival->width,
                   frmival->height);
        return 0;
    }

    builtin->pixfmt = frmival->pixel_format;

    return 0;
}

static int camera_builtin_enum_control(QEMUCamera *camera,
               QEMUCameraControl *controls, int ncontrols, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    QEMUCameraControl *control;
    QEMUCameraControlType t;

    for (t = 0; t < QEMUCameraControlMax; t++) {
        control = controls + t;
        control->type = t;
        control->cur = builtin->ctrl[t][ATTRIBUTE_CUR];
        control->def = builtin->ctrl[t][ATTRIBUTE_DEF];
        control->min = builtin->ctrl[t][ATTRIBUTE_MIN];
        control->max = builtin->ctrl[t][ATTRIBUTE_MAX];
        control->step = builtin->ctrl[t][ATTRIBUTE_STEP];
    }

    return t;
}

static int camera_builtin_set_control(QEMUCamera *camera,
               const QEMUCameraControl *control, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);

    builtin->ctrl[control->type][ATTRIBUTE_CUR] = control->cur;

    return 0;
}

#define CHECK_AND_GET_OPTS(x, y)                               \
    do {                                                       \
        if (builtinopts->has_##x) {                            \
            if (builtinopts->x > CAMERA_BUILTIN_MAX_##y        \
               || builtinopts->x < CAMERA_BUILTIN_MIN_##y) {   \
                error_setg(errp, "%s: unsupported %s(%d, %d)", \
                           TYPE_CAMERA_BUILTIN, #x,            \
                           CAMERA_BUILTIN_MIN_##y,             \
                           CAMERA_BUILTIN_MAX_##y);            \
                return;                                        \
            }                                                  \
            builtin->x = builtinopts->x;                       \
        } else {                                               \
            builtin->x = CAMERA_BUILTIN_DEF_##y;               \
        }                                                      \
    } while (0)

#define CHECK_AND_GET_VAL(x, def)                              \
    do {                                                       \
        if (builtinopts->has_##x) {                            \
            builtin->x = builtinopts->x;                       \
        } else {                                               \
            builtin->x = def;                                  \
        }                                                      \
    } while (0)

static void camera_builtin_open(QEMUCamera *camera, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    CameraBuiltinOptions *builtinopts = &camera->dev->u.builtin;

    CHECK_AND_GET_OPTS(width, WIDTH);
    CHECK_AND_GET_OPTS(height, HEIGHT);
    CHECK_AND_GET_OPTS(fps, FPS);
    CHECK_AND_GET_VAL(bgcolor, COLOR_TYPE_BLUE);
    CHECK_AND_GET_VAL(debug, false);
    CHECK_AND_GET_VAL(yuv, true);
    CHECK_AND_GET_VAL(rgb565, true);
#ifdef CONFIG_VNC_JPEG
    CHECK_AND_GET_VAL(mjpeg, true);
#else
    if (builtinopts->has_mjpeg && builtinopts->mjpeg) {
        error_setg(errp, "%s: no libjpeg supported", TYPE_CAMERA_BUILTIN);
        return;
    }
#endif

    if (builtin->mjpeg) {
        builtin->pixfmts[builtin->npixfmts++] = QEMU_CAMERA_PIX_FMT_MJPEG;
    }

    if (builtin->yuv) {
        builtin->pixfmts[builtin->npixfmts++] = QEMU_CAMERA_PIX_FMT_YUYV;
    }

    if (builtin->rgb565) {
        builtin->pixfmts[builtin->npixfmts++] = QEMU_CAMERA_PIX_FMT_RGB565;
    }

    if (!builtin->npixfmts) {
        error_setg(errp, "%s: all formats disabled", TYPE_CAMERA_BUILTIN);
    }
}

static void camera_builtin_stream_on(QEMUCamera *camera, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    cairo_format_t cairofmt;
    size_t imagesize;

    imagesize = builtin->height * builtin->width * 2;
    if (builtin->pixfmt == QEMU_CAMERA_PIX_FMT_RGB565) {
        cairofmt = CAIRO_FORMAT_RGB16_565;
        builtin->pixbytes = 2;
    } else {
        cairofmt = CAIRO_FORMAT_RGB24;
        builtin->pixbytes = 4; /* see enum cairo_format_t in cairo.h */
    }
    builtin->surface = cairo_image_surface_create(cairofmt, builtin->width,
                                                  builtin->height);
    builtin->cr = cairo_create(builtin->surface);
    qemu_camera_alloc_image(camera, imagesize, errp);
    builtin->image_size = imagesize;
    builtin->image = g_malloc(imagesize);

    builtin->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                               camera_builtin_frame_timer, camera);
    timer_mod(builtin->frame_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
              + NANOSECONDS_PER_SECOND / builtin->fps);
}

static void camera_builtin_stream_off(QEMUCamera *camera, Error **errp)
{
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);

    timer_free(builtin->frame_timer);

    qemu_camera_free_image(camera);
    g_free(builtin->image);
    builtin->image = NULL;
    builtin->sequence = 0;

    cairo_destroy(builtin->cr);
    cairo_surface_destroy(builtin->surface);

    g_free(builtin->opaque);
    builtin->opaque = NULL;
}


static void camera_builtin_init(Object *obj)
{
    QEMUCamera *camera = CAMERADEV(obj);
    CameraBuiltin *builtin = CAMERA_BUILTIN_DEV(camera);
    int i;

    /*
     * Because builtin camera is designed for debug purpose only, so this table
     * does't keep align with the read camera, just to make the code easy.
     */
    for (i = 0; i < QEMUCameraControlMax; i++) {
        builtin->ctrl[i][ATTRIBUTE_DEF] = 0x7f;
        builtin->ctrl[i][ATTRIBUTE_MIN] = 0;
        builtin->ctrl[i][ATTRIBUTE_MAX] = 0xff;
        builtin->ctrl[i][ATTRIBUTE_CUR] = 0;
        builtin->ctrl[i][ATTRIBUTE_STEP] = 1;
    }

    builtin->ctrl[QEMUCameraHueAuto][ATTRIBUTE_DEF] = 0;
    builtin->ctrl[QEMUCameraHueAuto][ATTRIBUTE_MIN] = 0;
    builtin->ctrl[QEMUCameraHueAuto][ATTRIBUTE_MAX] = 1;
}

static void camera_builtin_finalize(Object *obj)
{
    QEMUCamera *camera = CAMERADEV(obj);
    Error *local_err = NULL;

    camera_builtin_stream_off(camera, &local_err);
}

static void camera_builtin_class_init(ObjectClass *oc, void *data)
{
    QEMUCameraClass *klass = CAMERADEV_CLASS(oc);

    klass->open = camera_builtin_open;
    klass->stream_on = camera_builtin_stream_on;
    klass->stream_off = camera_builtin_stream_off;
    klass->enum_pixel_format = camera_builtin_enum_pixel_format;
    klass->enum_frame_size = camera_builtin_enum_frame_size;
    klass->enum_frame_interval = camera_builtin_enum_frame_interval;
    klass->set_frame_interval = camera_builtin_set_frame_interval;
    klass->enum_control = camera_builtin_enum_control;
    klass->set_control = camera_builtin_set_control;
}

static const TypeInfo camera_builtin_type_info = {
    .name = TYPE_CAMERA_BUILTIN,
    .parent = TYPE_CAMERADEV,
    .instance_size = sizeof(CameraBuiltin),
    .instance_init = camera_builtin_init,
    .instance_finalize = camera_builtin_finalize,
    .class_init = camera_builtin_class_init,
};

static void register_types(void)
{
    type_register_static(&camera_builtin_type_info);
}

type_init(register_types);
