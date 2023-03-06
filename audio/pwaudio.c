/*
 * QEMU Pipewire audio driver
 *
 * Copyright (c) 2023 Red Hat Inc.
 *
 * Author: Dorinda Bassey       <dbassey@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "audio.h"
#include <errno.h>
#include "qemu/error-report.h"
#include <spa/param/audio/format-utils.h>
#include <spa/utils/ringbuffer.h>
#include <spa/utils/result.h>

#include <pipewire/pipewire.h>
#include "trace.h"

#define AUDIO_CAP "pipewire"
#define RINGBUFFER_SIZE    (1u << 22)
#define RINGBUFFER_MASK    (RINGBUFFER_SIZE - 1)
#define BUFFER_SAMPLES    512

#include "audio_int.h"

enum {
    MODE_SINK,
    MODE_SOURCE
};

typedef struct pwaudio {
    Audiodev *dev;
    struct pw_thread_loop *thread_loop;
    struct pw_context *context;

    struct pw_core *core;
    struct spa_hook core_listener;
    int seq;
} pwaudio;

typedef struct PWVoice {
    pwaudio *g;
    bool enabled;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_audio_info_raw info;
    uint32_t frame_size;
    struct spa_ringbuffer ring;
    uint8_t buffer[RINGBUFFER_SIZE];

    uint32_t mode;
    struct pw_properties *props;
} PWVoice;

typedef struct PWVoiceOut {
    HWVoiceOut hw;
    PWVoice v;
} PWVoiceOut;

typedef struct PWVoiceIn {
    HWVoiceIn hw;
    PWVoice v;
} PWVoiceIn;

static void
stream_destroy(void *data)
{
    PWVoice *v = (PWVoice *) data;
    spa_hook_remove(&v->stream_listener);
    v->stream = NULL;
}

/* output data processing function to read stuffs from the buffer */
static void
playback_on_process(void *data)
{
    PWVoice *v = (PWVoice *) data;
    void *p;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    uint32_t n_frames, req, index, n_bytes;
    int32_t avail;

    if (!v->stream) {
        return;
    }

    /* obtain a buffer to read from */
    b = pw_stream_dequeue_buffer(v->stream);
    if (b == NULL) {
        error_report("out of buffers: %s", strerror(errno));
        return;
    }

    buf = b->buffer;
    p = buf->datas[0].data;
    if (p == NULL) {
        return;
    }
    req = b->requested * v->frame_size;
    if (req == 0) {
        req = 4096 * v->frame_size;
    }
    n_frames = SPA_MIN(req, buf->datas[0].maxsize);
    n_bytes = n_frames * v->frame_size;

    /* get no of available bytes to read data from buffer */

    avail = spa_ringbuffer_get_read_index(&v->ring, &index);

    if (!v->enabled) {
        avail = 0;
    }

    if (avail == 0) {
        memset(p, 0, n_bytes);
    } else {
        if (avail < (int32_t) n_bytes) {
            n_bytes = avail;
        }

        spa_ringbuffer_read_data(&v->ring,
                                    v->buffer, RINGBUFFER_SIZE,
                                    index & RINGBUFFER_MASK, p, n_bytes);

        index += n_bytes;
        spa_ringbuffer_read_update(&v->ring, index);
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = v->frame_size;
    buf->datas[0].chunk->size = n_bytes;

    /* queue the buffer for playback */
    pw_stream_queue_buffer(v->stream, b);
}

/* output data processing function to generate stuffs in the buffer */
static void
capture_on_process(void *data)
{
    PWVoice *v = (PWVoice *) data;
    void *p;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    int32_t filled;
    uint32_t index, offs, n_bytes;

    if (!v->stream) {
        return;
    }

    /* obtain a buffer */
    b = pw_stream_dequeue_buffer(v->stream);
    if (b == NULL) {
        error_report("out of buffers: %s", strerror(errno));
        return;
    }

    /* Write data into buffer */
    buf = b->buffer;
    p = buf->datas[0].data;
    if (p == NULL) {
        return;
    }
    offs = SPA_MIN(buf->datas[0].chunk->offset, buf->datas[0].maxsize);
    n_bytes = SPA_MIN(buf->datas[0].chunk->size, buf->datas[0].maxsize - offs);

    filled = spa_ringbuffer_get_write_index(&v->ring, &index);

    if (!v->enabled) {
        n_bytes = 0;
    }

    if (filled < 0) {
        error_report("%p: underrun write:%u filled:%d", p, index, filled);
    } else {
        if ((uint32_t) filled + n_bytes > RINGBUFFER_SIZE) {
            error_report("%p: overrun write:%u filled:%d + size:%u > max:%u",
            p, index, filled, n_bytes, RINGBUFFER_SIZE);
        }
    }
    spa_ringbuffer_write_data(&v->ring,
                                v->buffer, RINGBUFFER_SIZE,
                                index & RINGBUFFER_MASK,
                                SPA_PTROFF(p, offs, void), n_bytes);
    index += n_bytes;
    spa_ringbuffer_write_update(&v->ring, index);

    /* queue the buffer for playback */
    pw_stream_queue_buffer(v->stream, b);
}

static void
on_stream_state_changed(void *_data, enum pw_stream_state old,
                        enum pw_stream_state state, const char *error)
{
    PWVoice *v = (PWVoice *) _data;

    trace_pw_state_changed(pw_stream_state_as_string(state));

    switch (state) {
    case PW_STREAM_STATE_ERROR:
    case PW_STREAM_STATE_UNCONNECTED:
        {
            break;
        }
    case PW_STREAM_STATE_PAUSED:
        trace_pw_node(pw_stream_get_node_id(v->stream));
        break;
    case PW_STREAM_STATE_CONNECTING:
    case PW_STREAM_STATE_STREAMING:
        break;
    }
}

static const struct pw_stream_events capture_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .destroy = stream_destroy,
    .state_changed = on_stream_state_changed,
    .process = capture_on_process
};

static const struct pw_stream_events playback_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .destroy = stream_destroy,
    .state_changed = on_stream_state_changed,
    .process = playback_on_process
};

static size_t
qpw_read(HWVoiceIn *hw, void *data, size_t len)
{
    PWVoiceIn *pw = (PWVoiceIn *) hw;
    PWVoice *v = &pw->v;
    pwaudio *c = v->g;
    const char *error = NULL;
    size_t l;
    int32_t avail;
    uint32_t index;

    pw_thread_loop_lock(c->thread_loop);
    if (pw_stream_get_state(v->stream, &error) != PW_STREAM_STATE_STREAMING) {
        /* wait for stream to become ready */
        l = 0;
        goto done_unlock;
    }
    /* get no of available bytes to read data from buffer */
    avail = spa_ringbuffer_get_read_index(&v->ring, &index);

    trace_pw_read(avail, index, len);

    if (avail < (int32_t) len) {
        len = avail;
    }

    spa_ringbuffer_read_data(&v->ring,
                             v->buffer, RINGBUFFER_SIZE,
                             index & RINGBUFFER_MASK, data, len);
    index += len;
    spa_ringbuffer_read_update(&v->ring, index);
    l = len;

done_unlock:
    pw_thread_loop_unlock(c->thread_loop);
    return l;
}

static size_t
qpw_write(HWVoiceOut *hw, void *data, size_t len)
{
    PWVoiceOut *pw = (PWVoiceOut *) hw;
    PWVoice *v = &pw->v;
    pwaudio *c = v->g;
    const char *error = NULL;
    const int periods = 3;
    size_t l;
    int32_t filled, avail;
    uint32_t index;

    pw_thread_loop_lock(c->thread_loop);
    if (pw_stream_get_state(v->stream, &error) != PW_STREAM_STATE_STREAMING) {
        /* wait for stream to become ready */
        l = 0;
        goto done_unlock;
    }
    filled = spa_ringbuffer_get_write_index(&v->ring, &index);

    avail = BUFFER_SAMPLES * v->frame_size * periods - filled;

    trace_pw_write(filled, avail, index, len);

    if (len > avail) {
        len = avail;
    }

    if (filled < 0) {
        error_report("%p: underrun write:%u filled:%d", pw, index, filled);
    } else {
        if ((uint32_t) filled + len > RINGBUFFER_SIZE) {
            error_report("%p: overrun write:%u filled:%d + size:%zu > max:%u",
            pw, index, filled, len, RINGBUFFER_SIZE);
        }
    }

    spa_ringbuffer_write_data(&v->ring,
                                v->buffer, RINGBUFFER_SIZE,
                                index & RINGBUFFER_MASK, data, len);
    index += len;
    spa_ringbuffer_write_update(&v->ring, index);
    l = len;

done_unlock:
    pw_thread_loop_unlock(c->thread_loop);
    return l;
}

static int
audfmt_to_pw(AudioFormat fmt, int endianness)
{
    int format;

    switch (fmt) {
    case AUDIO_FORMAT_S8:
        format = SPA_AUDIO_FORMAT_S8;
        break;
    case AUDIO_FORMAT_U8:
        format = SPA_AUDIO_FORMAT_U8;
        break;
    case AUDIO_FORMAT_S16:
        format = endianness ? SPA_AUDIO_FORMAT_S16_BE : SPA_AUDIO_FORMAT_S16_LE;
        break;
    case AUDIO_FORMAT_U16:
        format = endianness ? SPA_AUDIO_FORMAT_U16_BE : SPA_AUDIO_FORMAT_U16_LE;
        break;
    case AUDIO_FORMAT_S32:
        format = endianness ? SPA_AUDIO_FORMAT_S32_BE : SPA_AUDIO_FORMAT_S32_LE;
        break;
    case AUDIO_FORMAT_U32:
        format = endianness ? SPA_AUDIO_FORMAT_U32_BE : SPA_AUDIO_FORMAT_U32_LE;
        break;
    case AUDIO_FORMAT_F32:
        format = endianness ? SPA_AUDIO_FORMAT_F32_BE : SPA_AUDIO_FORMAT_F32_LE;
        break;
    default:
        dolog("Internal logic error: Bad audio format %d\n", fmt);
        format = SPA_AUDIO_FORMAT_U8;
        break;
    }
    return format;
}

static AudioFormat
pw_to_audfmt(enum spa_audio_format fmt, int *endianness,
             uint32_t *frame_size)
{
    switch (fmt) {
    case SPA_AUDIO_FORMAT_S8:
        *frame_size = 1;
        return AUDIO_FORMAT_S8;
    case SPA_AUDIO_FORMAT_U8:
        *frame_size = 1;
        return AUDIO_FORMAT_U8;
    case SPA_AUDIO_FORMAT_S16_BE:
        *frame_size = 2;
        *endianness = 1;
        return AUDIO_FORMAT_S16;
    case SPA_AUDIO_FORMAT_S16_LE:
        *frame_size = 2;
        *endianness = 0;
        return AUDIO_FORMAT_S16;
    case SPA_AUDIO_FORMAT_U16_BE:
        *frame_size = 2;
        *endianness = 1;
        return AUDIO_FORMAT_U16;
    case SPA_AUDIO_FORMAT_U16_LE:
        *frame_size = 2;
        *endianness = 0;
        return AUDIO_FORMAT_U16;
    case SPA_AUDIO_FORMAT_S32_BE:
        *frame_size = 4;
        *endianness = 1;
        return AUDIO_FORMAT_S32;
    case SPA_AUDIO_FORMAT_S32_LE:
        *frame_size = 4;
        *endianness = 0;
        return AUDIO_FORMAT_S32;
    case SPA_AUDIO_FORMAT_U32_BE:
        *frame_size = 4;
        *endianness = 1;
        return AUDIO_FORMAT_U32;
    case SPA_AUDIO_FORMAT_U32_LE:
        *frame_size = 4;
        *endianness = 0;
        return AUDIO_FORMAT_U32;
    case SPA_AUDIO_FORMAT_F32_BE:
        *frame_size = 4;
        *endianness = 1;
        return AUDIO_FORMAT_F32;
    case SPA_AUDIO_FORMAT_F32_LE:
        *frame_size = 4;
        *endianness = 0;
        return AUDIO_FORMAT_F32;
    default:
        *frame_size = 1;
        dolog("Internal logic error: Bad spa_audio_format %d\n", fmt);
        return AUDIO_FORMAT_U8;
    }
}

static int
create_stream(pwaudio *c, PWVoice *v, const char *name)
{
    int res;
    uint32_t n_params;
    const struct spa_pod *params[2];
    uint8_t buffer[1024];
    struct spa_pod_builder b;

    v->stream = pw_stream_new(c->core, name, NULL);

    if (v->stream == NULL) {
        goto error;
    }

    if (v->mode == MODE_SOURCE) {
        pw_stream_add_listener(v->stream,
                            &v->stream_listener, &capture_stream_events, v);
    } else {
        pw_stream_add_listener(v->stream,
                            &v->stream_listener, &playback_stream_events, v);
    }

    n_params = 0;
    spa_pod_builder_init(&b, buffer, sizeof(buffer));
    params[n_params++] = spa_format_audio_raw_build(&b,
                            SPA_PARAM_EnumFormat,
                            &v->info);

    /* connect the stream to a sink or source */
    res = pw_stream_connect(v->stream,
                            v->mode ==
                            MODE_SOURCE ? PW_DIRECTION_INPUT :
                            PW_DIRECTION_OUTPUT, PW_ID_ANY,
                            PW_STREAM_FLAG_AUTOCONNECT |
                            PW_STREAM_FLAG_MAP_BUFFERS |
                            PW_STREAM_FLAG_RT_PROCESS, params, n_params);
    if (res < 0) {
        goto error;
    }

    return 0;
error:
    pw_stream_destroy(v->stream);
    return -1;
}

static int
qpw_stream_new(pwaudio *c, PWVoice *v, const char *name)
{
    int r;

    switch (v->info.channels) {
    case 8:
        v->info.position[0] = SPA_AUDIO_CHANNEL_FL;
        v->info.position[1] = SPA_AUDIO_CHANNEL_FR;
        v->info.position[2] = SPA_AUDIO_CHANNEL_FC;
        v->info.position[3] = SPA_AUDIO_CHANNEL_LFE;
        v->info.position[4] = SPA_AUDIO_CHANNEL_RL;
        v->info.position[5] = SPA_AUDIO_CHANNEL_RR;
        v->info.position[6] = SPA_AUDIO_CHANNEL_SL;
        v->info.position[7] = SPA_AUDIO_CHANNEL_SR;
        break;
    case 6:
        v->info.position[0] = SPA_AUDIO_CHANNEL_FL;
        v->info.position[1] = SPA_AUDIO_CHANNEL_FR;
        v->info.position[2] = SPA_AUDIO_CHANNEL_FC;
        v->info.position[3] = SPA_AUDIO_CHANNEL_LFE;
        v->info.position[4] = SPA_AUDIO_CHANNEL_RL;
        v->info.position[5] = SPA_AUDIO_CHANNEL_RR;
        break;
    case 5:
        v->info.position[0] = SPA_AUDIO_CHANNEL_FL;
        v->info.position[1] = SPA_AUDIO_CHANNEL_FR;
        v->info.position[2] = SPA_AUDIO_CHANNEL_FC;
        v->info.position[3] = SPA_AUDIO_CHANNEL_LFE;
        v->info.position[4] = SPA_AUDIO_CHANNEL_RC;
        break;
    case 4:
        v->info.position[0] = SPA_AUDIO_CHANNEL_FL;
        v->info.position[1] = SPA_AUDIO_CHANNEL_FR;
        v->info.position[2] = SPA_AUDIO_CHANNEL_FC;
        v->info.position[3] = SPA_AUDIO_CHANNEL_RC;
        break;
    case 3:
        v->info.position[0] = SPA_AUDIO_CHANNEL_FL;
        v->info.position[1] = SPA_AUDIO_CHANNEL_FR;
        v->info.position[2] = SPA_AUDIO_CHANNEL_LFE;
        break;
    case 2:
        v->info.position[0] = SPA_AUDIO_CHANNEL_FL;
        v->info.position[1] = SPA_AUDIO_CHANNEL_FR;
        break;
    case 1:
        v->info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        break;
    default:
        for (size_t i = 0; i < v->info.channels; i++) {
            v->info.position[i] = SPA_AUDIO_CHANNEL_UNKNOWN;
        }
        break;
    }

    /* create a new unconnected pwstream */
    r = create_stream(c, v, name);
    if (r < 0) {
        goto error;
    }

    return r;

error:
    AUD_log(AUDIO_CAP, "Failed to create stream.");
    return -1;
}

static int
qpw_init_out(HWVoiceOut *hw, struct audsettings *as, void *drv_opaque)
{
    PWVoiceOut *pw = (PWVoiceOut *) hw;
    PWVoice *v = &pw->v;
    struct audsettings obt_as = *as;
    pwaudio *c = v->g = drv_opaque;
    AudiodevPipewireOptions *popts = &c->dev->u.pipewire;
    AudiodevPipewirePerDirectionOptions *ppdo = popts->out;
    int r;
    v->enabled = false;

    v->mode = MODE_SINK;

    pw_thread_loop_lock(c->thread_loop);

    v->info.format = audfmt_to_pw(as->fmt, as->endianness);
    v->info.channels = as->nchannels;
    v->info.rate = as->freq;

    obt_as.fmt =
        pw_to_audfmt(v->info.format, &obt_as.endianness, &v->frame_size);
    v->frame_size *= as->nchannels;

    /* call the function that creates a new stream for playback */
    r = qpw_stream_new(c, v, ppdo->stream_name ? : c->dev->id);
    if (r < 0) {
        error_report("qpw_stream_new for playback failed\n ");
        goto fail;
    }

    /* report the audio format we support */
    audio_pcm_init_info(&hw->info, &obt_as);

    /* report the buffer size to qemu */
    hw->samples = BUFFER_SAMPLES;

    pw_thread_loop_unlock(c->thread_loop);
    return 0;
fail:
    pw_thread_loop_unlock(c->thread_loop);
    return -1;
}

static int
qpw_init_in(HWVoiceIn *hw, struct audsettings *as, void *drv_opaque)
{
    PWVoiceIn *pw = (PWVoiceIn *) hw;
    PWVoice *v = &pw->v;
    struct audsettings obt_as = *as;
    pwaudio *c = v->g = drv_opaque;
    AudiodevPipewireOptions *popts = &c->dev->u.pipewire;
    AudiodevPipewirePerDirectionOptions *ppdo = popts->in;
    int r;
    v->enabled = false;

    v->mode = MODE_SOURCE;
    pw_thread_loop_lock(c->thread_loop);

    v->info.format = audfmt_to_pw(as->fmt, as->endianness);
    v->info.channels = as->nchannels;
    v->info.rate = as->freq;

    obt_as.fmt =
        pw_to_audfmt(v->info.format, &obt_as.endianness, &v->frame_size);
    v->frame_size *= as->nchannels;

    /* call the function that creates a new stream for recording */
    r = qpw_stream_new(c, v, ppdo->stream_name ? : c->dev->id);
    if (r < 0) {
        error_report("qpw_stream_new for recording failed\n ");
        goto fail;
    }

    /* report the audio format we support */
    audio_pcm_init_info(&hw->info, &obt_as);

    /* report the buffer size to qemu */
    hw->samples = BUFFER_SAMPLES;

    pw_thread_loop_unlock(c->thread_loop);
    return 0;
fail:
    pw_thread_loop_unlock(c->thread_loop);
    return -1;
}

static void
qpw_fini_out(HWVoiceOut *hw)
{
    PWVoiceOut *pw = (PWVoiceOut *) hw;
    PWVoice *v = &pw->v;

    if (v->stream) {
        pwaudio *c = v->g;
        pw_thread_loop_lock(c->thread_loop);
        pw_stream_destroy(v->stream);
        v->stream = NULL;
        pw_thread_loop_unlock(c->thread_loop);
    }
}

static void
qpw_fini_in(HWVoiceIn *hw)
{
    PWVoiceIn *pw = (PWVoiceIn *) hw;
    PWVoice *v = &pw->v;

    if (v->stream) {
        pwaudio *c = v->g;
        pw_thread_loop_lock(c->thread_loop);
        pw_stream_destroy(v->stream);
        v->stream = NULL;
        pw_thread_loop_unlock(c->thread_loop);
    }
}

static void
qpw_enable_out(HWVoiceOut *hw, bool enable)
{
    PWVoiceOut *po = (PWVoiceOut *) hw;
    PWVoice *v = &po->v;
    v->enabled = enable;
}

static void
qpw_enable_in(HWVoiceIn *hw, bool enable)
{
    PWVoiceIn *pi = (PWVoiceIn *) hw;
    PWVoice *v = &pi->v;
    v->enabled = enable;
}

static void
on_core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
    pwaudio *pw = data;

    error_report("error id:%u seq:%d res:%d (%s): %s",
                id, seq, res, spa_strerror(res), message);

    pw_thread_loop_signal(pw->thread_loop, FALSE);
}

static void
on_core_done(void *data, uint32_t id, int seq)
{
    pwaudio *pw = data;
    if (id == PW_ID_CORE) {
        pw->seq = seq;
        pw_thread_loop_signal(pw->thread_loop, FALSE);
    }
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
    .error = on_core_error,
};

static void *
qpw_audio_init(Audiodev *dev)
{
    pwaudio *pw;
    pw = g_new0(pwaudio, 1);
    pw_init(NULL, NULL);

    AudiodevPipewireOptions *popts;
    trace_pw_audio_init("Initialize Pipewire context\n");
    assert(dev->driver == AUDIODEV_DRIVER_PIPEWIRE);
    popts = &dev->u.pipewire;

    if (!popts->has_latency) {
        popts->has_latency = true;
        popts->latency = 15000;
    }

    pw->dev = dev;
    pw->thread_loop = pw_thread_loop_new("Pipewire thread loop", NULL);
    if (pw->thread_loop == NULL) {
        error_report("Could not create Pipewire loop");
        goto fail_loop;
    }

    pw->context =
        pw_context_new(pw_thread_loop_get_loop(pw->thread_loop), NULL, 0);
    if (pw->context == NULL) {
        error_report("Could not create Pipewire context");
        goto fail_loop;
    }

    if (pw_thread_loop_start(pw->thread_loop) < 0) {
        error_report("Could not start Pipewire loop");
        goto fail_start;
    }

    pw_thread_loop_lock(pw->thread_loop);

    pw->core = pw_context_connect(pw->context, NULL, 0);
    if (pw->core == NULL) {
        goto fail_conn;
    }

    pw_core_add_listener(pw->core, &pw->core_listener, &core_events, pw);

    pw_thread_loop_unlock(pw->thread_loop);

    return pw;

fail_loop:
    pw_thread_loop_destroy(pw->thread_loop);
    g_free(pw);
    return NULL;
fail_start:
    pw_context_destroy(pw->context);
    g_free(pw);
    return NULL;
fail_conn:
    AUD_log(AUDIO_CAP, "Failed to initialize PW context");
    pw_thread_loop_unlock(pw->thread_loop);
    pw_thread_loop_stop(pw->thread_loop);
    pw_core_disconnect(pw->core);
    pw_context_destroy(pw->context);
    pw_thread_loop_destroy(pw->thread_loop);
    g_free(pw);
    return NULL;
}

static void
qpw_audio_fini(void *opaque)
{
    pwaudio *pw = opaque;

    pw_thread_loop_stop(pw->thread_loop);

    if (pw->core) {
        spa_hook_remove(&pw->core_listener);
        spa_zero(pw->core_listener);
        pw_core_disconnect(pw->core);
    }

    if (pw->context) {
        pw_context_destroy(pw->context);
    }
    pw_thread_loop_destroy(pw->thread_loop);

    g_free(pw);
}

static struct audio_pcm_ops qpw_pcm_ops = {
    .init_out = qpw_init_out,
    .fini_out = qpw_fini_out,
    .write = qpw_write,
    .buffer_get_free = audio_generic_buffer_get_free,
    .run_buffer_out = audio_generic_run_buffer_out,
    .enable_out = qpw_enable_out,

    .init_in = qpw_init_in,
    .fini_in = qpw_fini_in,
    .read = qpw_read,
    .run_buffer_in = audio_generic_run_buffer_in,
    .enable_in = qpw_enable_in
};

static struct audio_driver pw_audio_driver = {
    .name = "pipewire",
    .descr = "http://www.pipewire.org/",
    .init = qpw_audio_init,
    .fini = qpw_audio_fini,
    .pcm_ops = &qpw_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in = INT_MAX,
    .voice_size_out = sizeof(PWVoiceOut),
    .voice_size_in = sizeof(PWVoiceIn),
};

static void
register_audio_pw(void)
{
    audio_driver_register(&pw_audio_driver);
}

type_init(register_audio_pw);
