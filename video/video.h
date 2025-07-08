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

#ifndef QEMU_VIDEO_H
#define QEMU_VIDEO_H

#include "qemu/osdep.h"
#include "hw/qdev-properties-system.h"
#include "qom/object.h"
#include "qemu/queue.h"

/*
 * only to be used after vd->id has been initialized!
 */
#define vd_error_setg(vd, errp, fmt, ...) \
    error_setg(errp, "%s: %s: " fmt, TYPE_VIDEODEV, qemu_videodev_get_id(vd), ## __VA_ARGS__)

#define fourcc_code(a, b, c, d) \
                          ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                          ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/*
 * custom return codes emitted by Videodev
 * */
#define VIDEODEV_RC_OK        0 // everything ok
#define VIDEODEV_RC_ERROR    -1 // generic error code
#define VIDEODEV_RC_UNDERRUN -2 // streaming underrun
#define VIDEODEV_RC_NOTSUP   -3 // operation not supported
#define VIDEODEV_RC_INVAL    -4 // invalid argument

#define QEMU_VIDEO_PIX_FMT_YUYV   fourcc_code('Y', 'U', 'Y', 'V')
#define QEMU_VIDEO_PIX_FMT_NV12   fourcc_code('N', 'V', '1', '2')
#define QEMU_VIDEO_PIX_FMT_MJPEG  fourcc_code('M', 'J', 'P', 'G')
#define QEMU_VIDEO_PIX_FMT_RGB565 fourcc_code('R', 'G', 'B', 'P')

typedef enum VideoControlType {
    VideoControlTypeBrightness,
    VideoControlTypeContrast,
    VideoControlTypeGain,
    VideoControlTypeGamma,
    VideoControlTypeHue,
    VideoControlTypeHueAuto,
    VideoControlTypeSaturation,
    VideoControlTypeSharpness,
    VideoControlTypeWhiteBalanceTemperature,
    VideoControlTypeMax
} VideoControlType;

static inline bool qemu_video_pixfmt_supported(uint32_t pixfmt)
{
    switch (pixfmt) {
    case QEMU_VIDEO_PIX_FMT_YUYV:
    case QEMU_VIDEO_PIX_FMT_NV12:
    return true;
    }

    return false;
}

typedef struct VideoControl {
    VideoControlType type;
    int32_t cur;
    int32_t def;
    int32_t min;
    int32_t max;
    int32_t step;
} VideoControl;

typedef struct VideoFramerate {
    uint32_t numerator;
    uint32_t denominator;
} VideoFramerate;

typedef struct VideoFramesize {
    uint32_t width;
    uint32_t height;
    int nframerate;
    VideoFramerate *framerates;
} VideoFramesize;

typedef struct VideoModes {
    uint32_t pixelformat;
    int nframesize;
    VideoFramesize *framesizes;
} VideoMode;

typedef struct VideoStreamOptions {
    uint8_t format_index;
    uint8_t frame_index;
    uint32_t frame_interval;
} VideoStreamOptions;

typedef struct VideoFrameChunk {
    void *data;
    size_t size;
} VideoFrameChunk;

#define TYPE_VIDEODEV "videodev"
OBJECT_DECLARE_TYPE(Videodev, VideodevClass, VIDEODEV)

struct Videodev {
    Object parent_obj;

    char *id;
    bool registered;
    bool is_streaming;

    int nmodes;
    VideoMode *modes;

    int ncontrols;
    VideoControl *controls;

    struct SelectedStreamOptions {
        VideoMode *mode;
        VideoFramesize *frmsz;
        VideoFramerate frmrt;
    } selected;

    struct VideoFrame {
        uint8_t *data;
        size_t bytes_left;
    } current_frame;

    QLIST_ENTRY(Videodev) list;
};

struct VideodevClass {
    ObjectClass parent_class;

    /*
     * Called upon construction, consumes QemuOpts* and
     * opens/starts the backend
     *
     * on success:
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*open)(Videodev *vd, QemuOpts *opts, Error **errp);

    /*
     * [optional]
     * Called upon deconstruction, closes the backend and frees resources
     *
     * on success:
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*close)(Videodev *vd, Error **errp);

    /*
     * Enumerate all supported modes
     *
     * The backend-specific implementation has to allocate
     * and populate Videodev.modes (automatically free'd by
     * Videodev upon deconstruction)
     *
     * on success:
     *   creates and populates Videodev.modes
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*enum_modes)(Videodev *vd, Error **errp);

    /*
     * [optional]
     * Enumerate camera controls
     *
     * Allocates and populates Videodev.controls (automatically
     * free'd by Videodev upon deconstruction)
     *
     * on success:
     *   creates and populates Videodev.controls
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*enum_controls)(Videodev *vd, Error **errp);

    /*
     * [optional]
     * Set camera control settings
     *
     * on success:
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*set_control)(Videodev *vd, VideoControl *ctrl, Error **errp);

    /*
     * Start video capture stream
     *
     * This function enables the video streaming by following
     * a backend-specific procedure.
     *
     * on success:
     *   enables video streaming so that frames can be acquired
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*stream_on)(Videodev *vd, Error **errp);

    /*
     * Stop video capture stream
     *
     * Tbis function disables the video streaming. It is
     * the counterpart to stream_on.
     *
     * on success:
     *   disables video streaming, reverses stream_on
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*stream_off)(Videodev *vd, Error **errp);

    /*
     * Claim a single frame from the backend.
     *
     * An implementation of claim_frame must acquire the latest
     * frame from the backend.
     *
     * If no frame is ready to be claimed, VIDEODEV_RC_UNDERRUN shall be returned
     *
     * on success:
     *   set Videodev.current_frame.data to acquired frame
     *   set Videodev.current_frame.bytes_left to total size of acquired frame (> 0)
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   must not modify Videodev.current_frame
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*claim_frame)(Videodev *vd, Error **errp);

    /*
     * Release a previously acquired frame.
     *
     * An implementation of release_frame must cleanup the previously
     * acquired frame.
     *
     * on success:
     *   set Videodev.current_frame.data to NULL
     *   set Videodev.current_frame.bytes_left to 0
     *   returns VIDEODEV_RC_OK
     * on failure:
     *   must not modify Videodev.current_frame
     *   returns no VIDEODEV_RC_OK
     *   sets @errp accordingly
     * */
    int (*release_frame)(Videodev *vd, Error **errp);
};

/* ====== */

Videodev *qemu_videodev_new_from_opts(QemuOpts *opts, Error **errp);
int qemu_videodev_delete(Videodev *vd, Error **errp);
int qemu_videodev_set_control(Videodev *vd, VideoControl *ctrl, Error **errp);
bool qemu_videodev_check_options(Videodev *vd, VideoStreamOptions *opts);
int qemu_videodev_stream_on(Videodev *vd, VideoStreamOptions *opts, Error **errp);
int qemu_videodev_stream_off(Videodev *vd, Error **errp);
int qemu_videodev_read_frame(Videodev *vd, const size_t upto, VideoFrameChunk *chunk, Error **errp);
int qemu_videodev_read_frame_done(Videodev *vd, Error **errp);
size_t qemu_videodev_current_frame_length(Videodev *vd);
VideoFramerate *qemu_videodev_get_framerates(Videodev *vd, uint8_t format_idx, uint8_t frame_idx, int *n);

/* ====== */

char *qemu_videodev_get_id(Videodev *vd);
Videodev *qemu_videodev_by_id(char *id, Error **errp);
void qemu_videodev_register(Videodev *vd, Error **errp);

#define DEFINE_VIDEO_PROPERTIES(_s, _f)         \
    DEFINE_PROP_VIDEODEV("videodev", _s, _f)

#endif /* QEMU_VIDEO_H */
