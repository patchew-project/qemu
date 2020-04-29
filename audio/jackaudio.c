/*
 * QEMU JACK Audio Connection Kit Client
 *
 * Copyright (c) 2020 Geoffrey McRae (gnif)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/fifo8.h"
#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "jack"
#include "audio_int.h"

#include <stdatomic.h>
#include <jack/jack.h>
#include <jack/thread.h>

struct QJack;

typedef enum QJackState {
  QJACK_STATE_DISCONNECTED,
  QJACK_STATE_CONNECTED,
  QJACK_STATE_IDLE,
  QJACK_STATE_RUNNING,
  QJACK_STATE_STOPPING,
  QJACK_STATE_STOPPED,
}
QJackState;

typedef struct QJackBuffer {
  int          channels;
  int          frames;
  _Atomic(int) used;
  int          rptr, wptr;
  float      **data;
}
QJackBuffer;

typedef struct QJackClient {
  bool            out;
  QJackState      state;
  jack_client_t  *client;
  jack_nframes_t  freq;

  struct QJack   *j;
  int             nchannels;
  int             buffersize;
  jack_port_t   **port;
  QJackBuffer     fifo;
}
QJackClient;

typedef struct QJackOut {
    HWVoiceOut  hw;
    QJackClient c;
}
QJackOut;

typedef struct QJackIn {
    HWVoiceIn   hw;
    QJackClient c;
}
QJackIn;

static void qjack_buffer_create(QJackBuffer *buffer, int channels, int frames)
{
    buffer->channels = channels;
    buffer->frames   = frames;
    buffer->used     = 0;
    buffer->rptr     = 0;
    buffer->wptr     = 0;
    buffer->data     = g_malloc(channels * sizeof(float *));
    for (int i = 0; i < channels; ++i) {
        buffer->data[i] = g_malloc(frames * sizeof(float));
    }
}

static void qjack_buffer_clear(QJackBuffer *buffer)
{
    atomic_store_explicit(&buffer->used, 0, memory_order_relaxed);
    buffer->rptr = 0;
    buffer->wptr = 0;
}

static void qjack_buffer_free(QJackBuffer *buffer)
{
    for (int i = 0; i < buffer->channels; ++i) {
        g_free(buffer->data[i]);
    }

    g_free(buffer->data);
    buffer->data = NULL;
}

static inline int qjack_buffer_used(QJackBuffer *buffer)
{
    return atomic_load_explicit(&buffer->used, memory_order_relaxed);
}

/* write PCM interleaved */
static int qjack_buffer_write(QJackBuffer *buffer, float *data, int size)
{
    const int samples = size / sizeof(float);
    int frames        = samples / buffer->channels;
    const int avail   = buffer->frames -
        atomic_load_explicit(&buffer->used, memory_order_acquire);

    if (frames > avail) {
        frames = avail;
    }

    int copy = frames;
    int wptr = buffer->wptr;

    while (copy) {

        for (int c = 0; c < buffer->channels; ++c) {
            buffer->data[c][wptr] = *data++;
        }

        if (++wptr == buffer->frames) {
            wptr = 0;
        }

        --copy;
    }

    buffer->wptr = wptr;

    atomic_fetch_add_explicit(&buffer->used, frames, memory_order_release);
    return frames * buffer->channels * sizeof(float);
};

/* write PCM linear */
static int qjack_buffer_write_l(QJackBuffer *buffer, float **dest, int frames)
{
    const int avail   = buffer->frames -
        atomic_load_explicit(&buffer->used, memory_order_acquire);
    int wptr = buffer->wptr;

    if (frames > avail) {
        frames = avail;
    }

    int right = buffer->frames - wptr;
    if (right > frames) {
        right = frames;
    }

    const int left = frames - right;
    for (int c = 0; c < buffer->channels; ++c) {
        memcpy(buffer->data[c] + wptr, dest[c]        , right * sizeof(float));
        memcpy(buffer->data[c]       , dest[c] + right, left  * sizeof(float));
    }

    wptr += frames;
    if (wptr >= buffer->frames) {
        wptr -= buffer->frames;
    }
    buffer->wptr = wptr;

    atomic_fetch_add_explicit(&buffer->used, frames, memory_order_release);
    return frames;
}

/* read PCM interleaved */
static int qjack_buffer_read(QJackBuffer *buffer, float *dest, int size)
{
    const int samples = size / sizeof(float);
    int frames        = samples / buffer->channels;
    const int avail   =
        atomic_load_explicit(&buffer->used, memory_order_acquire);

    if (frames > avail) {
        frames = avail;
    }

    int copy = frames;
    int rptr = buffer->rptr;

    while (copy) {

        for (int c = 0; c < buffer->channels; ++c) {
            *dest++ = buffer->data[c][rptr];
        }

        if (++rptr == buffer->frames) {
            rptr = 0;
        }

        --copy;
    }

    buffer->rptr = rptr;

    atomic_fetch_sub_explicit(&buffer->used, frames, memory_order_release);
    return frames * buffer->channels * sizeof(float);
}

/* read PCM linear */
static int qjack_buffer_read_l(QJackBuffer *buffer, float **dest, int frames)
{
    int copy       = frames;
    const int used = atomic_load_explicit(&buffer->used, memory_order_acquire);
    int rptr       = buffer->rptr;

    if (copy > used) {
        copy = used;
    }

    int right = buffer->frames - rptr;
    if (right > copy) {
        right = copy;
    }

    const int left = copy - right;
    for (int c = 0; c < buffer->channels; ++c) {
        memcpy(dest[c]        , buffer->data[c] + rptr, right * sizeof(float));
        memcpy(dest[c] + right, buffer->data[c]       , left  * sizeof(float));
    }

    rptr += copy;
    if (rptr >= buffer->frames) {
        rptr -= buffer->frames;
    }
    buffer->rptr = rptr;

    atomic_fetch_sub_explicit(&buffer->used, copy, memory_order_release);
    return copy;
}

static int qjack_process(jack_nframes_t nframes, void *arg)
{
    QJackClient *c = (QJackClient *)arg;
    jack_transport_state_t ts = jack_transport_query(c->client, NULL);

    if (ts == JackTransportRolling) {

        if (c->state != QJACK_STATE_RUNNING) {
            return 0;
        }

        /* get the buffers for the ports */
        float *buffers[c->nchannels];
        for (int i = 0; i < c->nchannels; ++i) {
            buffers[i] = jack_port_get_buffer(c->port[i], nframes);
        }

        if (c->out) {
            qjack_buffer_read_l(&c->fifo, buffers, nframes);
        } else {
            qjack_buffer_write_l(&c->fifo, buffers, nframes);
        }

    } else if (ts == JackTransportStopped) {

        if (c->state == QJACK_STATE_RUNNING) {
            c->state = QJACK_STATE_STOPPED;
        }

    }

    return 0;
}

static int qjack_xrun(void *arg)
{
    QJackClient *c = (QJackClient *)arg;

    if (c->state != QJACK_STATE_RUNNING) {
        return 0;
    }

    qjack_buffer_clear(&c->fifo);
    return 0;
}

static void qjack_shutdown(void *arg)
{
    QJackClient *c = (QJackClient *)arg;

    switch (c->state) {
    case QJACK_STATE_RUNNING:
        jack_transport_stop(c->client);
        /* fallthrough */

    case QJACK_STATE_STOPPED:
        /* fallthrough */

    case QJACK_STATE_IDLE:
        jack_deactivate(c->client);
        /* fallthrough */

    default:
        jack_client_close(c->client);
        c->state = QJACK_STATE_DISCONNECTED;
        dolog("shutdown\n");
        break;

    case QJACK_STATE_DISCONNECTED:
        break;
    }
}

static size_t qjack_write(HWVoiceOut *hw, void *buf, size_t len)
{
    QJackOut *jo = (QJackOut *)hw;

    if (jo->c.state != QJACK_STATE_RUNNING) {
        return len;
    }

    return qjack_buffer_write(&jo->c.fifo, buf, len);
}

static size_t qjack_read(HWVoiceIn *hw, void *buf, size_t len)
{
    QJackIn *ji = (QJackIn *)hw;

    if (ji->c.state != QJACK_STATE_RUNNING) {
        return len;
    }

    return qjack_buffer_read(&ji->c.fifo, buf, len);
}

static int qjack_client_init(QJackClient *c,
    AudiodevJackPerDirectionOptions *opt, bool out, struct audsettings *as)
{
    jack_status_t status;
    char client_name[jack_client_name_size()];
    jack_options_t options = JackNullOption;

    c->out = out;

    snprintf(client_name, sizeof(client_name), "%s-%s",
        out ? "out" : "in",
        opt->client_name ? opt->client_name : qemu_get_vm_name());

    if (opt->exact_name) {
        options |= JackUseExactName;
    }

    if (!opt->start_server) {
        options |= JackNoStartServer;
    }

    if (opt->server_name) {
        options |= JackServerName;
    }

    c->client = jack_client_open(client_name, options, &status,
      opt->server_name);

    if (c->client == NULL) {
        dolog("jack_client_open failed: status = 0x%2.0x\n", status);
        if (status & JackServerFailed) {
            dolog("unable to connect to JACK server\n");
        }
        return -1;
    }

    c->freq = jack_get_sample_rate(c->client);

    if (status & JackServerStarted) {
        dolog("JACK server started\n");
    }

    if (status & JackNameNotUnique) {
        dolog("JACK unique name assigned %s\n",
          jack_get_client_name(c->client));
    }

    jack_set_process_callback(c->client, qjack_process , c);
    jack_set_xrun_callback(c->client, qjack_xrun    , c);
    jack_on_shutdown(c->client, qjack_shutdown, c);

    c->state = QJACK_STATE_CONNECTED;

    /* activate the session */
    jack_activate(c->client);

    c->nchannels  = as->nchannels;
    c->buffersize = jack_get_buffer_size(c->client);

    /*
     * ensure the buffersize is no smaller then 512 samples, some (all?) qemu
     * virtual devices do not work correctly otherwise
     */
    if (c->buffersize < 512) {
        c->buffersize = 512;
    }

    /* create a 2 period buffer */
    qjack_buffer_create(&c->fifo, c->nchannels, c->buffersize * 2);

    /* allocate and register the ports */
    c->port = g_malloc(sizeof(jack_port_t *) * c->nchannels);
    for (int i = 0; i < c->nchannels; ++i) {

        char port_name[16];
        snprintf(
            port_name,
            sizeof(port_name),
            out ? "output %d" : "input %d",
            i);

        c->port[i] = jack_port_register(
            c->client,
            port_name,
            JACK_DEFAULT_AUDIO_TYPE,
            out ? JackPortIsOutput : JackPortIsInput,
            0);
    }

    c->state = QJACK_STATE_IDLE;
    return 0;
}

static int qjack_init_out(HWVoiceOut *hw, struct audsettings *as,
    void *drv_opaque)
{
    QJackOut *jo  = (QJackOut *)hw;
    Audiodev *dev = (Audiodev *)drv_opaque;

    if (jo->c.state != QJACK_STATE_DISCONNECTED) {
        return 0;
    }

    int ret = qjack_client_init(&jo->c, dev->u.jack.out, true, as);
    if (ret != 0) {
        return ret;
    }

    /* report the buffer size to qemu */
    hw->samples = jo->c.buffersize;

    /* report the audio format we support */
    struct audsettings os = {
        .freq       = jo->c.freq,
        .nchannels  = jo->c.nchannels,
        .fmt        = AUDIO_FORMAT_F32,
        .endianness = 0
    };
    audio_pcm_init_info(&hw->info, &os);

    dolog("JACK output configured for %dHz (%d samples)\n",
        jo->c.freq, jo->c.buffersize);

    return 0;
}

static int qjack_init_in(HWVoiceIn *hw, struct audsettings *as,
    void *drv_opaque)
{
    QJackIn  *ji  = (QJackIn *)hw;
    Audiodev *dev = (Audiodev *)drv_opaque;

    if (ji->c.state != QJACK_STATE_DISCONNECTED) {
        return 0;
    }

    int ret = qjack_client_init(&ji->c, dev->u.jack.in, false, as);
    if (ret != 0) {
        return ret;
    }

    /* report the buffer size to qemu */
    hw->samples = ji->c.buffersize;

    /* report the audio format we support */
    struct audsettings is = {
        .freq       = ji->c.freq,
        .nchannels  = ji->c.nchannels,
        .fmt        = AUDIO_FORMAT_F32,
        .endianness = 0
    };
    audio_pcm_init_info(&hw->info, &is);

    dolog("JACK input configured for %dHz (%d samples)\n",
        ji->c.freq, ji->c.buffersize);

    return 0;
}


static void qjack_client_fini(QJackClient *c)
{
    qjack_shutdown(c);

    qjack_buffer_free(&c->fifo);
    for (int i = 0; i < c->nchannels; ++i) {
        jack_port_unregister(c->client, c->port[i]);
    }
    g_free(c->port);
}

static void qjack_fini_out(HWVoiceOut *hw)
{
    QJackOut *jo = (QJackOut *)hw;
    qjack_client_fini(&jo->c);
}

static void qjack_fini_in(HWVoiceIn *hw)
{
    QJackIn *ji = (QJackIn *)hw;
    qjack_client_fini(&ji->c);
}

static void qjack_enable(QJackClient *c, bool enable)
{
    if (c->state != QJACK_STATE_IDLE) {
        return;
    }

    if (enable) {
        jack_transport_start(c->client);
        c->state = QJACK_STATE_RUNNING;
    } else {
        jack_transport_stop(c->client);
        c->state = QJACK_STATE_IDLE;
    }
}

static void qjack_enable_out(HWVoiceOut *hw, bool enable)
{
    QJackOut *jo = (QJackOut *)hw;
    qjack_enable(&jo->c, enable);
}

static void qjack_enable_in(HWVoiceIn *hw, bool enable)
{
    QJackIn *ji = (QJackIn *)hw;
    qjack_enable(&ji->c, enable);
}

static int qjack_thread_creator(jack_native_thread_t *thread,
    const pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
    int ret = pthread_create(thread, attr, function, arg);
    if (ret != 0) {
        return ret;
    }

    /* set the name of the thread */
    pthread_setname_np(*thread, "jack-client");

    return ret;
}

static void *qjack_init(Audiodev *dev)
{
    assert(dev->driver == AUDIODEV_DRIVER_JACK);

    dev->u.jack.has_in = false;

    return dev;
}

static void qjack_fini(void *opaque)
{
}

static struct audio_pcm_ops jack_pcm_ops = {
    .init_out       = qjack_init_out,
    .fini_out       = qjack_fini_out,
    .write          = qjack_write,
    .run_buffer_out = audio_generic_run_buffer_out,
    .enable_out     = qjack_enable_out,

    .init_in        = qjack_init_in,
    .fini_in        = qjack_fini_in,
    .read           = qjack_read,
    .enable_in      = qjack_enable_in
};

static struct audio_driver jack_driver = {
    .name           = "jack",
    .descr          = "JACK Audio Connection Kit Client",
    .init           = qjack_init,
    .fini           = qjack_fini,
    .pcm_ops        = &jack_pcm_ops,
    .can_be_default = 1,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof(QJackOut),
    .voice_size_in  = sizeof(QJackIn)
};

static void register_audio_jack(void)
{
    audio_driver_register(&jack_driver);
    jack_set_thread_creator(qjack_thread_creator);
}
type_init(register_audio_jack);
