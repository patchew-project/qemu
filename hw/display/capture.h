#ifndef CAPTURE_H
#define CAPTURE_H

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "qemu/timer.h"
#include "audio/audio.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include <libswscale/swscale.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>

void hmp_capture_start(Monitor *mon, const QDict *qdict);
void hmp_capture_stop(Monitor *mon, const QDict *qdict);

typedef struct OutputStream {
    AVStream *st;
    int samples_count;
    AVFrame *frame;
    AVFrame *tmp_frame;
    AVFrame *empty_frame;
    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

struct CaptureThreadWorkerData {
    QEMUTimer *timer;
    int frame;
    int is_loaded;
    int is_capturing;
    int framerate;
    double video_len;
    double video_len2;
    CaptureState *wavCapture;

    AVCodec *codec;
    AVCodecContext *c;

    AVFrame *picture;
    AVPacket pkt;

    AVCodec *audio_codec;
    OutputStream stream;
    OutputStream audio_stream;
    AVFormatContext *oc;

    int64_t time;
    double delta;

    void *soundCapture;
};

typedef struct {
    int bytes;
    CaptureVoiceOut *cap;
    struct CaptureThreadWorkerData *data;
    int bufferPos;
} SoundCapture;

static int sound_capture_start_capture(struct CaptureThreadWorkerData *data);
static int ends_with(const char *str, const char *suffix);
static struct CaptureThreadWorkerData *capture_get_data(void);
static void write_delayed_audio_frames(void);
static void write_delayed_video_frames(void);
static int capture_stop(void);
static double write_audio_frame(AVFormatContext *oc, OutputStream *ost);
static void write_empty_sound(void *opaque, struct CaptureThreadWorkerData* data);

#endif
