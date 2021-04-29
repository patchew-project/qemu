/*
 * Virtio Sound device
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/virtio/virtio.h"
#include "audio/audio.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/option.h"
#include "qemu/option_int.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qdict.h"
#include "hw/virtio/virtio-snd.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/error.h"
#include "qapi/qapi-events-audio.h"
#include "hw/qdev-properties.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-events-migration.h"
#include "migration/misc.h"
#include "standard-headers/linux/ethtool.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "monitor/qdev.h"
#include "hw/pci/pci.h"
#include "intel-hda-defs.h"

#define VIRTIO_SOUND_VM_VERSION 1

#define VIRTIO_SOUND_JACK_DEFAULT 0
#define VIRTIO_SOUND_STREAM_DEFAULT 1
#define VIRTIO_SOUND_CHMAP_DEFAULT 0

#define VIRTIO_SOUND_HDA_FN_NID_OUT 0
#define VIRTIO_SOUND_HDA_FN_NID_IN 1

#define virtio_snd_log(...) AUD_log("virtio sound info", __VA_ARGS__)
#define virtio_snd_warn(...) AUD_log("virtio sound warn", __VA_ARGS__)
#define virtio_snd_err(...) AUD_log("virtio sound err", __VA_ARGS__)

static void virtio_snd_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOSound *s = VIRTIO_SOUND(vdev);
    virtio_snd_config sndcfg;

    memset(&sndcfg, 0, sizeof(virtio_snd_config));
    stl_le_p(&(sndcfg.jacks), s->snd_conf.jacks);
    stl_le_p(&(sndcfg.streams), s->snd_conf.streams);
    stl_le_p(&(sndcfg.chmaps), s->snd_conf.chmaps);

    memcpy(config, &sndcfg, sizeof(virtio_snd_config));
}

static void virtio_snd_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIOSound *s = VIRTIO_SOUND(vdev);
    virtio_snd_config sndcfg;

    memcpy(&sndcfg, config, sizeof(virtio_snd_config));

    memcpy(&s->snd_conf.jacks, &sndcfg.jacks, sizeof(uint32_t));
    memcpy(&s->snd_conf.streams, &sndcfg.streams, sizeof(uint32_t));
    memcpy(&s->snd_conf.chmaps, &sndcfg.streams, sizeof(uint32_t));
}

static const VMStateDescription vmstate_virtio_snd_device = {
    .name = "virtio-snd-device",
    .version_id = VIRTIO_SOUND_VM_VERSION,
    .minimum_version_id = VIRTIO_SOUND_VM_VERSION,
};

static const VMStateDescription vmstate_virtio_snd = {
    .name = "virtio-sound",
    .minimum_version_id = VIRTIO_SOUND_VM_VERSION,
    .version_id = VIRTIO_SOUND_VM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_snd_properties[] = {
    DEFINE_AUDIO_PROPERTIES(VirtIOSound, card),
    DEFINE_PROP_UINT32("jacks", VirtIOSound, snd_conf.jacks,
                       VIRTIO_SOUND_JACK_DEFAULT),
    DEFINE_PROP_UINT32("streams", VirtIOSound, snd_conf.streams,
                       VIRTIO_SOUND_STREAM_DEFAULT),
    DEFINE_PROP_UINT32("chmaps", VirtIOSound, snd_conf.chmaps,
                       VIRTIO_SOUND_CHMAP_DEFAULT),
    DEFINE_PROP_END_OF_LIST(),
};

static uint64_t virtio_snd_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    return vdev->host_features;
}
/*
 * Get a specific jack from the VirtIOSound card.
 *
 * @s: VirtIOSound card device.
 * @id: Jack id
 */
static virtio_snd_jack *virtio_snd_get_jack(VirtIOSound *s, uint32_t id)
{
    if (id >= s->snd_conf.jacks) {
        return NULL;
    }
    return s->jacks[id];
}

/*
 * Handles VIRTIO_SND_R_JACK_INFO.
 * The function writes the info structs and response to the virtqueue element.
 * Returns the used size in bytes.
 *
 * @s: VirtIOSound card
 * @elem: The request element from control queue
 */
static uint32_t virtio_snd_handle_jack_info(VirtIOSound *s,
                                            VirtQueueElement *elem)
{
    virtio_snd_query_info req;
    size_t sz = iov_to_buf(elem->out_sg, elem->out_num, 0, &req, sizeof(req));
    assert(sz == sizeof(virtio_snd_query_info));

    virtio_snd_hdr resp;

    if (iov_size(elem->in_sg, elem->in_num) <
        sizeof(virtio_snd_hdr) + req.count * req.size) {
        virtio_snd_err("jack info: buffer too small got: %lu needed: %lu\n",
                       iov_size(elem->in_sg, elem->in_num),
                       sizeof(virtio_snd_hdr) + req.count * req.size);
        resp.code = VIRTIO_SND_S_BAD_MSG;
        goto done;
    }

    virtio_snd_jack_info *jack_info = g_new0(virtio_snd_jack_info, req.count);
    for (int i = req.start_id; i < req.count + req.start_id; i++) {
        virtio_snd_jack *jack = virtio_snd_get_jack(s, i);
        if (!jack) {
            virtio_snd_err("Invalid jack id: %d\n", i);
            resp.code = VIRTIO_SND_S_BAD_MSG;
            goto done;
        }

        jack_info[i - req.start_id].hdr.hda_fn_nid = jack->hda_fn_nid;
        jack_info[i - req.start_id].features = jack->features;
        jack_info[i - req.start_id].hda_reg_defconf = jack->hda_reg_defconf;
        jack_info[i - req.start_id].hda_reg_caps = jack->hda_reg_caps;
        jack_info[i - req.start_id].connected = jack->connected;
        memset(jack_info[i - req.start_id].padding, 0,
               sizeof(jack_info[i - req.start_id].padding));
    }

    resp.code = VIRTIO_SND_S_OK;
done:
    sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));
    assert(sz == sizeof(virtio_snd_hdr));

    if (resp.code == VIRTIO_SND_S_BAD_MSG) {
        g_free(jack_info);
        return sz;
    }

    sz = iov_from_buf(elem->in_sg, elem->in_num, sizeof(virtio_snd_hdr),
                      jack_info, sizeof(virtio_snd_jack_info) * req.count);
    assert(sz == req.count * req.size);
    g_free(jack_info);
    return sizeof(virtio_snd_hdr) + sz;
}

/*
 * Handles VIRTIO_SND_R_JACK_REMAP.
 * Not implemented yet.
 *
 * @s: VirtIOSound card
 * @elem: The request element from control queue
 */
static uint32_t virtio_snd_handle_jack_remap(VirtIOSound *s,
                                             VirtQueueElement *elem)
{
    virtio_snd_hdr resp;
    resp.code = VIRTIO_SND_S_OK;

    /* TODO: implement remap */

    size_t sz;
    sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));
    assert(sz == sizeof(virtio_snd_hdr));
    return sz;
}

/*
 * Get a specific stream from the virtio sound card device.
 *
 * @s: VirtIOSound card device
 * @stream: Stream id
 *
 * Returns NULL if function fails.
 * TODO: Make failure more explicit. Output can be NULL if the stream number
 *       is valid but the stream hasn't been allocated yet.
 */
static virtio_snd_pcm_stream *virtio_snd_pcm_get_stream(VirtIOSound *s,
                                                        uint32_t stream)
{
    if (stream >= s->snd_conf.streams) {
        virtio_snd_err("Invalid stream request %d\n", stream);
        return NULL;
    }
    return s->streams[stream];
}

/*
 * Handle the VIRTIO_SND_R_PCM_INFO request.
 * The function writes the info structs to the request element.
 * Returns the used size in bytes.
 *
 * @s: VirtIOSound card device
 * @elem: The request element from control queue
 */
static uint32_t virtio_snd_handle_pcm_info(VirtIOSound *s,
                                           VirtQueueElement *elem)
{
    virtio_snd_query_info req;
    uint32_t sz;
    sz = iov_to_buf(elem->out_sg, elem->out_num, 0, &req, sizeof(req));
    assert(sz == sizeof(virtio_snd_query_info));

    virtio_snd_hdr resp;
    if (iov_size(elem->in_sg, elem->in_num) <
        sizeof(virtio_snd_hdr) + req.size * req.count) {
        virtio_snd_err("pcm info: buffer too small, got: %lu, needed: %lu\n",
                iov_size(elem->in_sg, elem->in_num),
                sizeof(virtio_snd_pcm_info));
        resp.code = VIRTIO_SND_S_BAD_MSG;
        goto done;
    }

    virtio_snd_pcm_stream *stream;
    virtio_snd_pcm_info *pcm_info = g_new0(virtio_snd_pcm_info, req.count);
    for (int i = req.start_id; i < req.start_id + req.count; i++) {
        stream = virtio_snd_pcm_get_stream(s, i);

        if (!stream) {
            virtio_snd_err("Invalid stream id: %d\n", i);
            resp.code = VIRTIO_SND_S_BAD_MSG;
            goto done;
        }

        pcm_info[i - req.start_id].hdr.hda_fn_nid = stream->hda_fn_nid;
        pcm_info[i - req.start_id].features = stream->features;
        pcm_info[i - req.start_id].formats = stream->formats;
        pcm_info[i - req.start_id].rates = stream->rates;
        pcm_info[i - req.start_id].direction = stream->direction;
        pcm_info[i - req.start_id].channels_min = stream->channels_min;
        pcm_info[i - req.start_id].channels_max = stream->channels_max;

        memset(&pcm_info[i].padding, 0, sizeof(pcm_info[i].padding));
    }

    resp.code = VIRTIO_SND_S_OK;
done:
    sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));
    assert(sz == sizeof(virtio_snd_hdr));

    if (resp.code == VIRTIO_SND_S_BAD_MSG) {
        g_free(pcm_info);
        return sz;
    }

    sz = iov_from_buf(elem->in_sg, elem->in_num, sizeof(virtio_snd_hdr),
                      pcm_info, sizeof(virtio_snd_pcm_info) * req.count);
    assert(sz == req.size * req.count);
    g_free(pcm_info);
    return sizeof(virtio_snd_hdr) + sz;
}

/*
 * Set the given stream params.
 * Called by both virtio_snd_handle_pcm_set_params and during device
 * initialization.
 * Returns a virtio sound status VIRTIO_SND_S_*
 *
 * @s: VirtIOSound card device
 * @params: The PCM params as defined in the virtio specification
 */
static uint32_t virtio_snd_pcm_set_params_impl(VirtIOSound *s,
                                               virtio_snd_pcm_set_params *params)
{
    uint32_t st = params->hdr.stream_id;
    if (st > s->snd_conf.streams || !(s->pcm_params)) {
        virtio_error(VIRTIO_DEVICE(s), "Streams not initalized\n");
        return VIRTIO_SND_S_BAD_MSG;
    }

    if (!s->pcm_params[st]) {
        s->pcm_params[st] = g_new0(virtio_snd_pcm_params, 1);
    }
    virtio_snd_pcm_params *st_params = s->pcm_params[st];

    st_params->features = params->features;
    st_params->buffer_bytes = params->buffer_bytes;
    st_params->period_bytes = params->period_bytes;

    if (params->channel < 1 || params->channel > AUDIO_MAX_CHANNELS) {
        virtio_snd_err("Number of channels not supported\n");
        return VIRTIO_SND_S_NOT_SUPP;
    }
    st_params->channel = params->channel;

    uint32_t supported_formats = 1 << VIRTIO_SND_PCM_FMT_S8 |
                                 1 << VIRTIO_SND_PCM_FMT_U8 |
                                 1 << VIRTIO_SND_PCM_FMT_S16 |
                                 1 << VIRTIO_SND_PCM_FMT_U16 |
                                 1 << VIRTIO_SND_PCM_FMT_S32 |
                                 1 << VIRTIO_SND_PCM_FMT_U32 |
                                 1 << VIRTIO_SND_PCM_FMT_FLOAT;

    uint32_t supported_rates = 1 << VIRTIO_SND_PCM_RATE_5512 |
                               1 << VIRTIO_SND_PCM_RATE_8000 |
                               1 << VIRTIO_SND_PCM_RATE_11025 |
                               1 << VIRTIO_SND_PCM_RATE_16000 |
                               1 << VIRTIO_SND_PCM_RATE_22050 |
                               1 << VIRTIO_SND_PCM_RATE_32000 |
                               1 << VIRTIO_SND_PCM_RATE_44100 |
                               1 << VIRTIO_SND_PCM_RATE_48000 |
                               1 << VIRTIO_SND_PCM_RATE_64000 |
                               1 << VIRTIO_SND_PCM_RATE_88200 |
                               1 << VIRTIO_SND_PCM_RATE_96000 |
                               1 << VIRTIO_SND_PCM_RATE_176399 |
                               1 << VIRTIO_SND_PCM_RATE_192000 |
                               1 << VIRTIO_SND_PCM_RATE_384000;

    if (!(supported_formats & (1 << params->format))) {
        virtio_snd_err("Stream format not supported\n");
        return VIRTIO_SND_S_NOT_SUPP;
    }
    st_params->format = params->format;

    if (!(supported_rates & (1 << params->rate))) {
        virtio_snd_err("Stream rate not supported\n");
        return VIRTIO_SND_S_NOT_SUPP;
    }
    st_params->rate = params->rate;

    st_params->period_bytes = params->period_bytes;
    st_params->buffer_bytes = params->buffer_bytes;
    return VIRTIO_SND_S_OK;
}

/*
 * Handles the VIRTIO_SND_R_PCM_SET_PARAMS request.
 * The function writes the response to the virtqueue element.
 * Returns the used size in bytes.
 *
 * @s: VirtIOSound card device
 * @elem: The request element from control queue
 */
static uint32_t virtio_snd_handle_pcm_set_params(VirtIOSound *s,
                                                 VirtQueueElement *elem)
{
    virtio_snd_pcm_set_params req;
    uint32_t sz;
    sz = iov_to_buf(elem->out_sg, elem->out_num, 0, &req, sizeof(req));
    assert(sz == sizeof(virtio_snd_pcm_set_params));

    virtio_snd_hdr resp;
    resp.code = virtio_snd_pcm_set_params_impl(s, &req);

    sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));
    assert(sz == sizeof(virtio_snd_hdr));
    return sz;
}

/*
 * Get a QEMU Audiosystem compatible format value from a VIRTIO_SND_PCM_FMT_*
 */
static AudioFormat virtio_snd_get_qemu_format(uint32_t format)
{
    switch (format) {
    case VIRTIO_SND_PCM_FMT_U8:
        return AUDIO_FORMAT_U8;
    case VIRTIO_SND_PCM_FMT_S8:
        return AUDIO_FORMAT_S8;
    case VIRTIO_SND_PCM_FMT_U16:
        return AUDIO_FORMAT_U16;
    case VIRTIO_SND_PCM_FMT_S16:
        return AUDIO_FORMAT_S16;
    case VIRTIO_SND_PCM_FMT_U32:
        return AUDIO_FORMAT_U32;
    case VIRTIO_SND_PCM_FMT_S32:
        return AUDIO_FORMAT_S32;
    case VIRTIO_SND_PCM_FMT_FLOAT:
        return AUDIO_FORMAT_F32;
    default:
        return -1;
    }
}

/*
 * Get a QEMU Audiosystem compatible frequency value from a
 * VIRTIO_SND_PCM_RATE_*
 */
static uint32_t virtio_snd_get_qemu_freq(uint32_t rate)
{
    switch (rate) {
    case VIRTIO_SND_PCM_RATE_5512:
        return 5512;
    case VIRTIO_SND_PCM_RATE_8000:
        return 8000;
    case VIRTIO_SND_PCM_RATE_11025:
        return 11025;
    case VIRTIO_SND_PCM_RATE_16000:
        return 16000;
    case VIRTIO_SND_PCM_RATE_22050:
        return 22050;
    case VIRTIO_SND_PCM_RATE_32000:
        return 32000;
    case VIRTIO_SND_PCM_RATE_44100:
        return 44100;
    case VIRTIO_SND_PCM_RATE_48000:
        return 48000;
    case VIRTIO_SND_PCM_RATE_64000:
        return 64000;
    case VIRTIO_SND_PCM_RATE_88200:
        return 88200;
    case VIRTIO_SND_PCM_RATE_96000:
        return 96000;
    case VIRTIO_SND_PCM_RATE_176399:
        return 176399;
    case VIRTIO_SND_PCM_RATE_192000:
        return 192000;
    case VIRTIO_SND_PCM_RATE_384000:
        return 384000;
    default:
        return -1;
    }
}

/*
 * Get QEMU Audiosystem compatible audsettings from virtio based pcm stream
 * params.
 */
static void virtio_snd_get_qemu_audsettings(audsettings *as,
                                            virtio_snd_pcm_params *params)
{
    as->nchannels = params->channel;
    as->fmt = virtio_snd_get_qemu_format(params->format);
    as->freq = virtio_snd_get_qemu_freq(params->rate);
    as->endianness = AUDIO_HOST_ENDIANNESS;
}

/*
 * Get the maximum number of virtqueue elements that can be inserted
 * into a virtio sound pcm stream
 *
 * @st: virtio sound pcm stream
 */
static int virtio_snd_pcm_get_nelems(virtio_snd_pcm_stream *st)
{
    return st->buffer_bytes / st->period_bytes
           + !!(st->buffer_bytes % st->period_bytes);
}

/*
 * Get the size in bytes of the buffer that still has to be written.
 *
 * @st: virtio sound pcm stream
 */
static int virtio_snd_pcm_get_pending_bytes(virtio_snd_pcm_stream *st)
{
    int pending = st->r_pos - st->w_pos;
    return pending < 0 ? pending + st->buffer_bytes : pending;
}

/*
 * Get data from a stream of the virtio sound device.
 *
 * @st: VirtIOSound card stream
 * @offset: Start reading from this offseta in the stream (in bytes)
 * @buffer: Write to this buffer
 * @size: The number of bytes to read
 */
static void virtio_snd_pcm_get_buf(virtio_snd_pcm_stream *st, uint32_t offset,
                                      void *buffer, uint32_t size)
{
    int nelems = virtio_snd_pcm_get_nelems(st);
    int i = 0;
    while (offset > st->period_bytes) {
        offset -= st->period_bytes;
        i++;
        i %= nelems;
    }


    /*
     * If the size spans beyond the current virtqueue element, read everything
     * from the current virtqueue element and move to the next element. Repeat
     * until we have read the required size.
     */
    while (size) {
        int remaining = iov_size(st->elems[i]->out_sg, st->elems[i]->out_num)
                        - sizeof(virtio_snd_pcm_xfer) - offset;
        int to_read = MIN(remaining, size), wpos = 0;
        size_t sz;
        sz = iov_to_buf(st->elems[i]->out_sg, st->elems[i]->out_num,
                        sizeof(virtio_snd_pcm_xfer) + offset, buffer + wpos,
                        to_read);

        assert(sz == to_read);

        offset = 0;
        size -= to_read;
        wpos += to_read;
        i++;
        i %= nelems;
    }
}

/*
 * Handle a buffer after it has been written by AUD_write.
 * It writes the status for the I/O messages that have been completed and
 * marks the tx virtqueue elmenets as used. It notifies the device
 * about I/O completion.
 *
 * @st: VirtIOSound card stream
 * @size: Size that was written by AUD_write
 *        If size = 0, write for the last element failed
 */
static void virtio_snd_pcm_handle_buf_written(virtio_snd_pcm_stream *st,
                                              uint32_t size)
{
    int offset = st->w_pos, i = 0;
    int nelems = virtio_snd_pcm_get_nelems(st);

    while (offset >= st->period_bytes) {
        offset -= st->period_bytes;
        i++;
        i %= nelems;
    }

    virtio_snd_pcm_status status;
    size_t sz;
    if (!size) {
        status.status = VIRTIO_SND_S_IO_ERR;
        status.latency_bytes = 0;

        sz = iov_from_buf(st->elems[i]->in_sg, st->elems[i]->in_num, 0,
                          &status, sizeof(status));
        assert(sz == sizeof(virtio_snd_pcm_status));

        int push_size = iov_size(st->elems[i]->out_sg, st->elems[i]->out_num)
                        + sizeof(virtio_snd_pcm_status);

        virtqueue_push(st->s->tx_vq, st->elems[i], push_size);
        return;
    }

    status.status = VIRTIO_SND_S_OK;
    status.latency_bytes = 0;

    /*
     * If the written size spans beyond current element, update the status for
     * the current element, mark it as used and push it back on the tx
     * virtqueue. Notify the device about the I/O completion. Repeat until
     * the required bytes are handled.
     */
    while (size) {
        int curr_elem_size = iov_size(st->elems[i]->out_sg, st->elems[i]->out_num)
                             - sizeof(virtio_snd_pcm_xfer) - offset;
        if (size >= curr_elem_size) {
            sz = iov_from_buf(st->elems[i]->in_sg, st->elems[i]->in_num, 0,
                              &status, sizeof(status));
            assert(sz == sizeof(virtio_snd_pcm_status));

            int push_size = sizeof(virtio_snd_pcm_xfer) + curr_elem_size
                            + offset + sizeof(virtio_snd_pcm_status);

            virtqueue_push(st->s->tx_vq, st->elems[i], push_size);
            virtio_notify(VIRTIO_DEVICE(st->s), st->s->tx_vq);
            g_free(st->elems[i]);
            st->elems[i] = NULL;
            size -= curr_elem_size;
            virtio_snd_log("remains: %d\n", size);
            st->w_pos += curr_elem_size;
            st->w_pos %= st->buffer_bytes + 1;
            i++;
            i %= nelems;
            offset = 0;
        } else {
            st->w_pos += size;
            st->w_pos %= st->buffer_bytes + 1;
            break;
        }
    }
}

/*
 * Callback for AUD_open_out.
 * Reads a buffer from the VirtIOSound card stream and writes it
 * using AUD_write.
 *
 * @opaque: VirtIOSound card stream
 * @free: Size in bytes that can be written via AUD_write
 */
static void virtio_snd_output_cb(void *opaque, int free)
{
    int to_play, pending;
    virtio_snd_pcm_stream *st = opaque;

    pending = virtio_snd_pcm_get_pending_bytes(st);
    to_play = MIN(free, pending);

    while (to_play) {
        uint8_t *mixbuf = g_malloc0(to_play);
        virtio_snd_pcm_get_buf(st, st->w_pos, mixbuf, to_play);
        int wbytes = AUD_write(st->voice.out, mixbuf, to_play);
        to_play -= wbytes;
        virtio_snd_pcm_handle_buf_written(st, wbytes);
        g_free(mixbuf);
    }
}

/*
 * Prepares a VirtIOSound card stream.
 * Returns a virtio sound status (VIRTIO_SND_S_*).
 *
 * @s: VirtIOSound card
 * @stream: stream id
 */
static uint32_t virtio_snd_pcm_prepare_impl(VirtIOSound *s, uint32_t stream)
{
    if (!s->streams || !s->pcm_params || !s->pcm_params[stream]) {
        virtio_snd_err("Cannot prepare stream %d without params.\n", stream);
        return VIRTIO_SND_S_BAD_MSG;
    }

    uint32_t supported_formats = 1 << VIRTIO_SND_PCM_FMT_S8 |
                                 1 << VIRTIO_SND_PCM_FMT_U8 |
                                 1 << VIRTIO_SND_PCM_FMT_S16 |
                                 1 << VIRTIO_SND_PCM_FMT_U16 |
                                 1 << VIRTIO_SND_PCM_FMT_S32 |
                                 1 << VIRTIO_SND_PCM_FMT_U32 |
                                 1 << VIRTIO_SND_PCM_FMT_FLOAT;

    uint32_t supported_rates = 1 << VIRTIO_SND_PCM_RATE_5512 |
                               1 << VIRTIO_SND_PCM_RATE_8000 |
                               1 << VIRTIO_SND_PCM_RATE_11025 |
                               1 << VIRTIO_SND_PCM_RATE_16000 |
                               1 << VIRTIO_SND_PCM_RATE_22050 |
                               1 << VIRTIO_SND_PCM_RATE_32000 |
                               1 << VIRTIO_SND_PCM_RATE_44100 |
                               1 << VIRTIO_SND_PCM_RATE_48000 |
                               1 << VIRTIO_SND_PCM_RATE_64000 |
                               1 << VIRTIO_SND_PCM_RATE_88200 |
                               1 << VIRTIO_SND_PCM_RATE_96000 |
                               1 << VIRTIO_SND_PCM_RATE_176399 |
                               1 << VIRTIO_SND_PCM_RATE_192000 |
                               1 << VIRTIO_SND_PCM_RATE_384000;

    virtio_snd_pcm_stream *st = g_new0(virtio_snd_pcm_stream, 1);
    st->hda_fn_nid = VIRTIO_SOUND_HDA_FN_NID_OUT;
    st->features = 0;
    st->direction = stream <= s->snd_conf.streams / 2 ?
                    VIRTIO_SND_D_OUTPUT : VIRTIO_SND_D_INPUT;
    st->channels_min = 1;
    st->channels_max = AUDIO_MAX_CHANNELS;
    st->formats = supported_formats;
    st->rates = supported_rates;
    st->s = s;

    st->buffer_bytes = s->pcm_params[stream]->buffer_bytes;
    st->period_bytes = s->pcm_params[stream]->period_bytes;

    audsettings as;
    virtio_snd_get_qemu_audsettings(&as, s->pcm_params[stream]);

    if (st->direction == VIRTIO_SND_D_OUTPUT) {
        st->voice.out = AUD_open_out(&s->card,
                                     st->voice.out,
                                     "virtio_snd_card",
                                     st,
                                     virtio_snd_output_cb, &as);

    } else {
        /* st->voice.in = AUD_open_in(&s->card,
         *                            st->voice.in,
         *                            "virtio_snd_card",
         *                            st,
         *                            virtio_snd_input_cb,
         *                            &as);
         */
    }

    uint32_t nelems = virtio_snd_pcm_get_nelems(st);
    st->elems = g_new0(VirtQueueElement *, nelems);
    st->tail = -1;
    st->w_pos = 0;
    st->r_pos = 0;
    s->streams[stream] = st;

    return VIRTIO_SND_S_OK;
}

/*
 * Handles VIRTIO_SND_R_PCM_PREPARE.
 * The function writes the response to the virtqueue element.
 * Returns the used size in bytes.
 *
 * @s: VirtIOSound card
 * @elem: The request element from control queue
 */
static uint32_t virtio_snd_handle_pcm_prepare(VirtIOSound *s,
                                              VirtQueueElement *elem)
{
    virtio_snd_pcm_hdr req;
    size_t sz;

    sz = iov_to_buf(elem->out_sg, elem->out_num, 0, &req, sizeof(req));
    assert(sz == sizeof(virtio_snd_pcm_hdr));

    virtio_snd_hdr resp;
    resp.code = virtio_snd_pcm_prepare_impl(s, req.stream_id);
    sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));
    assert(sz == sizeof(virtio_snd_hdr));
    return sz;
}

/*
 * Handles VIRTIO_SND_R_PCM_START.
 * The function writes the response to the virtqueue element.
 * Returns the used size in bytes.
 *
 * @s: VirtIOSound card
 * @elem: The request element from control queue
 */
static uint32_t virtio_snd_handle_pcm_start(VirtIOSound *s,
                                            VirtQueueElement *elem)
{
    virtio_snd_pcm_hdr req;
    size_t sz;

    sz = iov_to_buf(elem->out_sg, elem->out_num, 0, &req, sizeof(req));
    assert(sz == sizeof(virtio_snd_pcm_hdr));

    virtio_snd_hdr resp;
    resp.code = VIRTIO_SND_S_OK;

    virtio_snd_pcm_stream *st = virtio_snd_pcm_get_stream(s, req.stream_id);
    AUD_set_active_out(st->voice.out, true);

    sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));
    assert(sz == sizeof(virtio_snd_hdr));
    return sz;
}

/* The control queue handler. Pops an element from the control virtqueue,
 * checks the header and performs the requested action. Finally marks the
 * element as used.
 *
 * @vdev: VirtIOSound card device
 * @vq: Control virtqueue
 */
static void virtio_snd_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOSound *s = VIRTIO_SOUND(vdev);
    virtio_snd_hdr ctrl;

    VirtQueueElement *elem = NULL;
    size_t sz;
    struct iovec *iov, *iov2;
    unsigned int iov_cnt;

    for (;;) {
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }
        if (iov_size(elem->in_sg, elem->in_num) < sizeof(ctrl) ||
                iov_size(elem->out_sg, elem->out_num) < sizeof(ctrl)) {
            virtio_snd_err("virtio-snd ctrl missing headers\n");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        iov_cnt = elem->out_num;
        iov2 = iov = g_memdup(elem->out_sg,
                              sizeof(struct iovec) * elem->out_num);
        sz = iov_to_buf(iov, iov_cnt, 0, &ctrl, sizeof(ctrl));
        iov_discard_front(&iov, &iov_cnt, sizeof(ctrl));
        if (sz != sizeof(ctrl)) {
            /* error */
            virtio_snd_err("virtio snd ctrl could not read header\n");
        } else if (ctrl.code == VIRTIO_SND_R_JACK_INFO) {
            sz = virtio_snd_handle_jack_info(s, elem);
            goto done;
        } else if (ctrl.code == VIRTIO_SND_R_JACK_REMAP) {
            sz = virtio_snd_handle_jack_remap(s, elem);
            goto done;
        } else if (ctrl.code == VIRTIO_SND_R_PCM_INFO) {
            sz = virtio_snd_handle_pcm_info(s, elem);
            goto done;
        } else if (ctrl.code == VIRTIO_SND_R_PCM_SET_PARAMS) {
            sz = virtio_snd_handle_pcm_set_params(s, elem);
            goto done;
        } else if (ctrl.code == VIRTIO_SND_R_PCM_PREPARE) {
            sz = virtio_snd_handle_pcm_prepare(s, elem);
            goto done;
        } else if (ctrl.code == VIRTIO_SND_R_PCM_START) {
            sz = virtio_snd_handle_pcm_start(s, elem);
            goto done;
        } else if (ctrl.code == VIRTIO_SND_R_PCM_STOP) {
            virtio_snd_log("VIRTIO_SND_R_PCM_STOP");
        } else if (ctrl.code == VIRTIO_SND_R_PCM_RELEASE) {
            virtio_snd_log("VIRTIO_SND_R_PCM_RELEASE");
        } else {
            /* error */
            virtio_snd_err("virtio snd header not recognized: %d\n", ctrl.code);
        }

        virtio_snd_hdr resp;
        resp.code = VIRTIO_SND_S_OK;
        sz = iov_from_buf(elem->in_sg, elem->in_num, 0, &resp, sizeof(resp));

done:
        virtqueue_push(vq, elem, sz);
        virtio_notify(vdev, vq);
        g_free(iov2);
        g_free(elem);
    }
}

/*
 * Initializes the VirtIOSound card device. Validates the configuration
 * passed by the command line. Initializes the virtqueues. Allocates resources
 * for and initializes streams, jacks and chmaps.
 *
 * @dev: VirtIOSound card device
 * @errp: Set if there is an error
 */
static void virtio_snd_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSound *s = VIRTIO_SOUND(dev);

    virtio_init(vdev, "virtio-snd", VIRTIO_ID_SOUND, sizeof(virtio_snd_config));

    /* set number of jacks and streams */
    if (s->snd_conf.jacks > 8) {
        error_setg(errp, "Invalid number of jacks: %d", s->snd_conf.jacks);
        return;
    }
    if (s->snd_conf.streams < 1 || s->snd_conf.streams > 10) {
        error_setg(errp, "Invalid number of streams: %d", s->snd_conf.streams);
        return;
    }

    if (s->snd_conf.chmaps > VIRTIO_SND_CHMAP_MAX_SIZE) {
        error_setg(errp, "Invalid number of channel maps: %d",
                   s->snd_conf.chmaps);
        return;
    }

    /* set up QEMUSoundCard and audiodev */
    AUD_register_card ("virtio_snd_card", &s->card);

    // set default params for all streams
    virtio_snd_pcm_set_params default_params;
    default_params.features = 0;
    default_params.buffer_bytes = 8192;
    default_params.period_bytes = 4096;
    default_params.channel = 2;
    default_params.format = VIRTIO_SND_PCM_FMT_S16;
    default_params.rate = VIRTIO_SND_PCM_RATE_44100;

    s->ctrl_vq = virtio_add_queue(vdev, 64, virtio_snd_handle_ctrl);

    s->streams = g_new0(virtio_snd_pcm_stream *, s->snd_conf.streams);
    s->pcm_params = g_new0(virtio_snd_pcm_params *, s->snd_conf.streams);
    s->jacks = g_new0(virtio_snd_jack *, s->snd_conf.jacks);

    uint32_t status;
    for (int i = 0; i < s->snd_conf.streams; i++) {
        default_params.hdr.stream_id = i;
        status = virtio_snd_pcm_set_params_impl(s, &default_params);
        if (status != VIRTIO_SND_S_OK) {
            error_setg(errp, "Can't initalize stream params.\n");
            return;
        }
        status = virtio_snd_pcm_prepare_impl(s, i);
        if (status != VIRTIO_SND_S_OK) {
            error_setg(errp, "Can't prepare streams.\n");
            return;
        }
    }

    for (int i = 0; i < s->snd_conf.jacks; i++) {
        // TODO: For now the hda_fn_nid connects the starting streams to these
        // jacks. This isn't working for now since the directions will be wrong
        // for a few jacks. Similarly the capabilities are just placeholders.
        s->jacks[i] = (virtio_snd_jack *)g_malloc0(sizeof(virtio_snd_jack));
        s->jacks[i]->features = 0;
        s->jacks[i]->hda_fn_nid = i;
        s->jacks[i]->hda_reg_defconf = ((AC_JACK_PORT_COMPLEX << AC_DEFCFG_PORT_CONN_SHIFT) |
                                       (AC_JACK_LINE_OUT     << AC_DEFCFG_DEVICE_SHIFT)    |
                                       (AC_JACK_CONN_1_8     << AC_DEFCFG_CONN_TYPE_SHIFT) |
                                       (AC_JACK_COLOR_GREEN  << AC_DEFCFG_COLOR_SHIFT)     |
                                       0x10);
        s->jacks[i]->hda_reg_caps = AC_PINCAP_OUT;
        s->jacks[i]->connected = false;
    }
}

static void virtio_snd_device_unrealize(DeviceState *dev)
{
}

static void virtio_snd_reset(VirtIODevice *vdev)
{
}

static void virtio_snd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_snd_properties);
    dc->vmsd = &vmstate_virtio_snd;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    vdc->realize = virtio_snd_device_realize;
    vdc->unrealize = virtio_snd_device_unrealize;
    vdc->get_config = virtio_snd_get_config;
    vdc->set_config = virtio_snd_set_config;
    vdc->get_features = virtio_snd_get_features;
    vdc->reset = virtio_snd_reset;
    vdc->legacy_features = 0;
    vdc->vmsd = &vmstate_virtio_snd_device;
}


static const TypeInfo virtio_snd_dev_info = {
    .name = TYPE_VIRTIO_SOUND,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOSound),
    .class_init = virtio_snd_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_snd_dev_info);
}

#undef virtio_snd_log
#undef virtio_snd_warn
#undef virtio_snd_err

type_init(virtio_register_types)
