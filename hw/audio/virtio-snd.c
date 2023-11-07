/*
 * VIRTIO Sound Device conforming to
 *
 * "Virtual I/O Device (VIRTIO) Version 1.2
 * Committee Specification Draft 01
 * 09 May 2022"
 *
 * <https://docs.oasis-open.org/virtio/virtio/v1.2/csd01/virtio-v1.2-csd01.html#x1-52900014>
 *
 * Copyright (c) 2023 Emmanouil Pitsidianakis <manos.pitsidianakis@linaro.org>
 * Copyright (C) 2019 OpenSynergy GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "include/qemu/lockable.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/audio/virtio-snd.h"
#include "hw/core/cpu.h"

#define VIRTIO_SOUND_VM_VERSION 1
#define VIRTIO_SOUND_JACK_DEFAULT 0
#define VIRTIO_SOUND_STREAM_DEFAULT 1
#define VIRTIO_SOUND_CHMAP_DEFAULT 0
#define VIRTIO_SOUND_HDA_FN_NID 0

static uint32_t supported_formats = BIT(VIRTIO_SND_PCM_FMT_S8)
                                  | BIT(VIRTIO_SND_PCM_FMT_U8)
                                  | BIT(VIRTIO_SND_PCM_FMT_S16)
                                  | BIT(VIRTIO_SND_PCM_FMT_U16)
                                  | BIT(VIRTIO_SND_PCM_FMT_S32)
                                  | BIT(VIRTIO_SND_PCM_FMT_U32)
                                  | BIT(VIRTIO_SND_PCM_FMT_FLOAT);

static uint32_t supported_rates = BIT(VIRTIO_SND_PCM_RATE_5512)
                                | BIT(VIRTIO_SND_PCM_RATE_8000)
                                | BIT(VIRTIO_SND_PCM_RATE_11025)
                                | BIT(VIRTIO_SND_PCM_RATE_16000)
                                | BIT(VIRTIO_SND_PCM_RATE_22050)
                                | BIT(VIRTIO_SND_PCM_RATE_32000)
                                | BIT(VIRTIO_SND_PCM_RATE_44100)
                                | BIT(VIRTIO_SND_PCM_RATE_48000)
                                | BIT(VIRTIO_SND_PCM_RATE_64000)
                                | BIT(VIRTIO_SND_PCM_RATE_88200)
                                | BIT(VIRTIO_SND_PCM_RATE_96000)
                                | BIT(VIRTIO_SND_PCM_RATE_176400)
                                | BIT(VIRTIO_SND_PCM_RATE_192000)
                                | BIT(VIRTIO_SND_PCM_RATE_384000);

static const VMStateDescription vmstate_virtio_snd_device = {
    .name = TYPE_VIRTIO_SND,
    .version_id = VIRTIO_SOUND_VM_VERSION,
    .minimum_version_id = VIRTIO_SOUND_VM_VERSION,
};

static const VMStateDescription vmstate_virtio_snd = {
    .name = TYPE_VIRTIO_SND,
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

static void
virtio_snd_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOSound *s = VIRTIO_SND(vdev);
    virtio_snd_config *sndconfig =
        (virtio_snd_config *)config;
    trace_virtio_snd_get_config(vdev,
                                s->snd_conf.jacks,
                                s->snd_conf.streams,
                                s->snd_conf.chmaps);

    memcpy(sndconfig, &s->snd_conf, sizeof(s->snd_conf));
    cpu_to_le32s(&sndconfig->jacks);
    cpu_to_le32s(&sndconfig->streams);
    cpu_to_le32s(&sndconfig->chmaps);

}

static void
virtio_snd_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIOSound *s = VIRTIO_SND(vdev);
    const virtio_snd_config *sndconfig =
        (const virtio_snd_config *)config;


   trace_virtio_snd_set_config(vdev,
                               s->snd_conf.jacks,
                               sndconfig->jacks,
                               s->snd_conf.streams,
                               sndconfig->streams,
                               s->snd_conf.chmaps,
                               sndconfig->chmaps);

    memcpy(&s->snd_conf, sndconfig, sizeof(virtio_snd_config));
    le32_to_cpus(&s->snd_conf.jacks);
    le32_to_cpus(&s->snd_conf.streams);
    le32_to_cpus(&s->snd_conf.chmaps);

}

static void
virtio_snd_ctrl_cmd_free(virtio_snd_ctrl_command *cmd)
{
    g_free(cmd->elem);
    g_free(cmd);
}

/*
 * Get a specific stream from the virtio sound card device.
 * Returns NULL if @stream_id is invalid or not allocated.
 *
 * @s: VirtIOSound device
 * @stream_id: stream id
 */
static VirtIOSoundPCMStream *virtio_snd_pcm_get_stream(VirtIOSound *s,
                                                       uint32_t stream_id)
{
    return stream_id >= s->snd_conf.streams ? NULL :
        s->pcm->streams[stream_id];
}

/*
 * Get params for a specific stream.
 *
 * @s: VirtIOSound device
 * @stream_id: stream id
 */
static virtio_snd_pcm_set_params *virtio_snd_pcm_get_params(VirtIOSound *s,
                                                            uint32_t stream_id)
{
    return stream_id >= s->snd_conf.streams ? NULL
        : &s->pcm->pcm_params[stream_id];
}

/*
 * Handle the VIRTIO_SND_R_PCM_INFO request.
 * The function writes the info structs to the request element.
 *
 * @s: VirtIOSound device
 * @cmd: The request command queue element from VirtIOSound cmdq field
 */
static void virtio_snd_handle_pcm_info(VirtIOSound *s,
                                       virtio_snd_ctrl_command *cmd)
{
    uint32_t stream_id, start_id, count, size;
    virtio_snd_pcm_info val;
    virtio_snd_query_info req;
    VirtIOSoundPCMStream *stream = NULL;
    g_autofree virtio_snd_pcm_info *pcm_info = NULL;
    size_t msg_sz = iov_to_buf(cmd->elem->out_sg,
                               cmd->elem->out_num,
                               0,
                               &req,
                               sizeof(virtio_snd_query_info));

    if (msg_sz != sizeof(virtio_snd_query_info)) {
        /*
         * TODO: do we need to set DEVICE_NEEDS_RESET?
         */
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: virtio-snd command size incorrect %zu vs \
                %zu\n", __func__, msg_sz, sizeof(virtio_snd_query_info));
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
        return;
    }

    start_id = le32_to_cpu(req.start_id);
    count = le32_to_cpu(req.count);
    size = le32_to_cpu(req.size);

    if (iov_size(cmd->elem->in_sg, cmd->elem->in_num) <
        sizeof(virtio_snd_hdr) + size * count) {
        /*
         * TODO: do we need to set DEVICE_NEEDS_RESET?
         */
        error_report("pcm info: buffer too small, got: %zu, needed: %zu",
                iov_size(cmd->elem->in_sg, cmd->elem->in_num),
                sizeof(virtio_snd_pcm_info));
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
        return;
    }

    pcm_info = g_new0(virtio_snd_pcm_info, count);
    for (uint32_t i = 0; i < count; i++) {
        stream_id = i + start_id;
        trace_virtio_snd_handle_pcm_info(stream_id);
        stream = virtio_snd_pcm_get_stream(s, stream_id);
        if (!stream) {
            error_report("Invalid stream id: %"PRIu32, stream_id);
            cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
            return;
        }
        val = stream->info;
        val.hdr.hda_fn_nid = cpu_to_le32(val.hdr.hda_fn_nid);
        val.features = cpu_to_le32(val.features);
        val.formats = cpu_to_le64(val.formats);
        val.rates = cpu_to_le64(val.rates);
        /*
         * 5.14.6.6.2.1 Device Requirements: Stream Information The device MUST
         * NOT set undefined feature, format, rate and direction values. The
         * device MUST initialize the padding bytes to 0.
         */
        pcm_info[i] = val;
        memset(&pcm_info[i].padding, 0, 5);
    }

    cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_OK);
    iov_from_buf(cmd->elem->in_sg,
                 cmd->elem->in_num,
                 sizeof(virtio_snd_hdr),
                 pcm_info,
                 sizeof(virtio_snd_pcm_info) * count);
}

/*
 * Set the given stream params.
 * Called by both virtio_snd_handle_pcm_set_params and during device
 * initialization.
 * Returns the response status code. (VIRTIO_SND_S_*).
 *
 * @s: VirtIOSound device
 * @params: The PCM params as defined in the virtio specification
 */
static
uint32_t virtio_snd_set_pcm_params(VirtIOSound *s,
                                   uint32_t stream_id,
                                   virtio_snd_pcm_set_params *params)
{
    virtio_snd_pcm_set_params *st_params;

    if (stream_id >= s->snd_conf.streams || s->pcm->pcm_params == NULL) {
        /*
         * TODO: do we need to set DEVICE_NEEDS_RESET?
         */
        virtio_error(VIRTIO_DEVICE(s), "Streams have not been initialized.\n");
        return cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
    }

    st_params = virtio_snd_pcm_get_params(s, stream_id);

    if (params->channels < 1 || params->channels > AUDIO_MAX_CHANNELS) {
        error_report("Number of channels is not supported.");
        return cpu_to_le32(VIRTIO_SND_S_NOT_SUPP);
    }
    if (!(supported_formats & BIT(params->format))) {
        error_report("Stream format is not supported.");
        return cpu_to_le32(VIRTIO_SND_S_NOT_SUPP);
    }
    if (!(supported_rates & BIT(params->rate))) {
        error_report("Stream rate is not supported.");
        return cpu_to_le32(VIRTIO_SND_S_NOT_SUPP);
    }

    st_params->buffer_bytes = le32_to_cpu(params->buffer_bytes);
    st_params->period_bytes = le32_to_cpu(params->period_bytes);
    st_params->features = le32_to_cpu(params->features);
    /* the following are uint8_t, so there's no need to bswap the values. */
    st_params->channels = params->channels;
    st_params->format = params->format;
    st_params->rate = params->rate;

    return cpu_to_le32(VIRTIO_SND_S_OK);
}

/*
 * Get a QEMU Audiosystem compatible format value from a VIRTIO_SND_PCM_FMT_*
 */
static AudioFormat virtio_snd_get_qemu_format(uint32_t format)
{
    #define CASE(FMT)               \
    case VIRTIO_SND_PCM_FMT_##FMT:  \
        return AUDIO_FORMAT_##FMT;

    switch (format) {
    CASE(U8)
    CASE(S8)
    CASE(U16)
    CASE(S16)
    CASE(U32)
    CASE(S32)
    case VIRTIO_SND_PCM_FMT_FLOAT:
        return AUDIO_FORMAT_F32;
    default:
        g_assert_not_reached();
    }

    #undef CASE
}

/*
 * Get a QEMU Audiosystem compatible frequency value from a
 * VIRTIO_SND_PCM_RATE_*
 */
static uint32_t virtio_snd_get_qemu_freq(uint32_t rate)
{
    #define CASE(RATE)               \
    case VIRTIO_SND_PCM_RATE_##RATE: \
        return RATE;

    switch (rate) {
    CASE(5512)
    CASE(8000)
    CASE(11025)
    CASE(16000)
    CASE(22050)
    CASE(32000)
    CASE(44100)
    CASE(48000)
    CASE(64000)
    CASE(88200)
    CASE(96000)
    CASE(176400)
    CASE(192000)
    CASE(384000)
    default:
        g_assert_not_reached();
    }

    #undef CASE
}

/*
 * Get QEMU Audiosystem compatible audsettings from virtio based pcm stream
 * params.
 */
static void virtio_snd_get_qemu_audsettings(audsettings *as,
                                            virtio_snd_pcm_set_params *params)
{
    as->nchannels = MIN(AUDIO_MAX_CHANNELS, params->channels);
    as->fmt = virtio_snd_get_qemu_format(params->format);
    as->freq = virtio_snd_get_qemu_freq(params->rate);
    as->endianness = target_words_bigendian() ? 1 : 0;
}

/*
 * Close a stream and free all its resources.
 *
 * @stream: VirtIOSoundPCMStream *stream
 */
static void virtio_snd_pcm_close(VirtIOSoundPCMStream *stream)
{
}

/*
 * Prepares a VirtIOSound card stream.
 * Returns the response status code. (VIRTIO_SND_S_*).
 *
 * @s: VirtIOSound device
 * @stream_id: stream id
 */
static uint32_t virtio_snd_pcm_prepare(VirtIOSound *s, uint32_t stream_id)
{
    audsettings as;
    virtio_snd_pcm_set_params *params;
    VirtIOSoundPCMStream *stream;

    if (s->pcm->streams == NULL ||
        s->pcm->pcm_params == NULL ||
        stream_id >= s->snd_conf.streams) {
        return cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
    }

    params = virtio_snd_pcm_get_params(s, stream_id);
    if (params == NULL) {
        return cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
    }

    stream = virtio_snd_pcm_get_stream(s, stream_id);
    if (stream == NULL) {
        stream = g_new0(VirtIOSoundPCMStream, 1);
        stream->active = false;
        stream->id = stream_id;
        stream->pcm = s->pcm;
        stream->s = s;

        /*
         * stream_id >= s->snd_conf.streams was checked before so this is
         * in-bounds
         */
        s->pcm->streams[stream_id] = stream;
    }

    virtio_snd_get_qemu_audsettings(&as, params);
    stream->info.direction = stream_id < s->snd_conf.streams / 2 +
        (s->snd_conf.streams & 1) ? VIRTIO_SND_D_OUTPUT : VIRTIO_SND_D_INPUT;
    stream->info.hdr.hda_fn_nid = VIRTIO_SOUND_HDA_FN_NID;
    stream->info.features = 0;
    stream->info.channels_min = 1;
    stream->info.channels_max = as.nchannels;
    stream->info.formats = supported_formats;
    stream->info.rates = supported_rates;
    stream->params = *params;

    stream->positions[0] = VIRTIO_SND_CHMAP_FL;
    stream->positions[1] = VIRTIO_SND_CHMAP_FR;
    stream->as = as;

    return cpu_to_le32(VIRTIO_SND_S_OK);
}

static const char *print_code(uint32_t code)
{
    #define CASE(CODE)            \
    case VIRTIO_SND_R_##CODE:     \
        return "VIRTIO_SND_R_"#CODE

    switch (code) {
    CASE(JACK_INFO);
    CASE(JACK_REMAP);
    CASE(PCM_INFO);
    CASE(PCM_SET_PARAMS);
    CASE(PCM_PREPARE);
    CASE(PCM_RELEASE);
    CASE(PCM_START);
    CASE(PCM_STOP);
    CASE(CHMAP_INFO);
    default:
        return "invalid code";
    }

    #undef CASE
};

/*
 * Handles VIRTIO_SND_R_PCM_START.
 *
 * @s: VirtIOSound device
 * @cmd: The request command queue element from VirtIOSound cmdq field
 * @start: whether to start or stop the device
 */
static void virtio_snd_handle_pcm_start_stop(VirtIOSound *s,
                                             virtio_snd_ctrl_command *cmd,
                                             bool start)
{
    VirtIOSoundPCMStream *stream;
    virtio_snd_pcm_hdr req;
    uint32_t stream_id;
    size_t msg_sz = iov_to_buf(cmd->elem->out_sg,
                               cmd->elem->out_num,
                               0,
                               &req,
                               sizeof(virtio_snd_pcm_hdr));

    if (msg_sz != sizeof(virtio_snd_pcm_hdr)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: virtio-snd command size incorrect %zu vs \
                %zu\n", __func__, msg_sz, sizeof(virtio_snd_pcm_hdr));
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
        return;
    }

    stream_id = le32_to_cpu(req.stream_id);
    cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_OK);
    trace_virtio_snd_handle_pcm_start_stop(start ? "VIRTIO_SND_R_PCM_START" :
            "VIRTIO_SND_R_PCM_STOP", stream_id);
    stream = virtio_snd_pcm_get_stream(s, stream_id);
    if (stream == NULL) {
        error_report("Invalid stream id: %"PRIu32, req.stream_id);
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
        return;
    }
    stream->active = start;
}

/*
 * The actual processing done in virtio_snd_process_cmdq().
 *
 * @s: VirtIOSound device
 * @cmd: control command request
 */
static inline void
process_cmd(VirtIOSound *s, virtio_snd_ctrl_command *cmd)
{
    uint32_t code;
    size_t msg_sz = iov_to_buf(cmd->elem->out_sg,
                               cmd->elem->out_num,
                               0,
                               &cmd->ctrl,
                               sizeof(virtio_snd_hdr));

    if (msg_sz != sizeof(virtio_snd_hdr)) {
        /*
         * TODO: do we need to set DEVICE_NEEDS_RESET?
         */
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: virtio-snd command size incorrect %zu vs \
                %zu\n", __func__, msg_sz, sizeof(virtio_snd_hdr));
        return;
    }

    code = le32_to_cpu(cmd->ctrl.code);

    trace_virtio_snd_handle_code(code, print_code(code));

    switch (code) {
    case VIRTIO_SND_R_JACK_INFO:
    case VIRTIO_SND_R_JACK_REMAP:
        qemu_log_mask(LOG_UNIMP,
                     "virtio_snd: jack functionality is unimplemented.\n");
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_NOT_SUPP);
        break;
    case VIRTIO_SND_R_PCM_INFO:
        virtio_snd_handle_pcm_info(s, cmd);
        break;
    case VIRTIO_SND_R_PCM_START:
        virtio_snd_handle_pcm_start_stop(s, cmd, true);
        break;
    case VIRTIO_SND_R_PCM_STOP:
        virtio_snd_handle_pcm_start_stop(s, cmd, false);
        break;
    case VIRTIO_SND_R_PCM_SET_PARAMS:
    case VIRTIO_SND_R_PCM_PREPARE:
    case VIRTIO_SND_R_PCM_RELEASE:
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_NOT_SUPP);
        break;
    case VIRTIO_SND_R_CHMAP_INFO:
        qemu_log_mask(LOG_UNIMP,
                     "virtio_snd: chmap info functionality is unimplemented.\n");
        trace_virtio_snd_handle_chmap_info();
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_NOT_SUPP);
        break;
    default:
        /* error */
        error_report("virtio snd header not recognized: %"PRIu32, code);
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_BAD_MSG);
    }

    iov_from_buf(cmd->elem->in_sg,
                 cmd->elem->in_num,
                 0,
                 &cmd->resp,
                 sizeof(virtio_snd_hdr));
    virtqueue_push(cmd->vq, cmd->elem, sizeof(virtio_snd_hdr));
    virtio_notify(VIRTIO_DEVICE(s), cmd->vq);
}

/*
 * Consume all elements in command queue.
 *
 * @s: VirtIOSound device
 */
static void virtio_snd_process_cmdq(VirtIOSound *s)
{
    virtio_snd_ctrl_command *cmd;

    if (unlikely(qatomic_read(&s->processing_cmdq))) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&s->cmdq_mutex) {
        qatomic_set(&s->processing_cmdq, true);
        while (!QTAILQ_EMPTY(&s->cmdq)) {
            cmd = QTAILQ_FIRST(&s->cmdq);

            /* process command */
            process_cmd(s, cmd);

            QTAILQ_REMOVE(&s->cmdq, cmd, next);

            virtio_snd_ctrl_cmd_free(cmd);
        }
        qatomic_set(&s->processing_cmdq, false);
    }
}

/*
 * The control message handler. Pops an element from the control virtqueue,
 * and stores them to VirtIOSound's cmdq queue and finally calls
 * virtio_snd_process_cmdq() for processing.
 *
 * @vdev: VirtIOSound device
 * @vq: Control virtqueue
 */
static void virtio_snd_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOSound *s = VIRTIO_SND(vdev);
    VirtQueueElement *elem;
    virtio_snd_ctrl_command *cmd;

    trace_virtio_snd_handle_ctrl(vdev, vq);

    if (!virtio_queue_ready(vq)) {
        return;
    }

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    while (elem) {
        cmd = g_new0(virtio_snd_ctrl_command, 1);
        cmd->elem = elem;
        cmd->vq = vq;
        cmd->resp.code = cpu_to_le32(VIRTIO_SND_S_OK);
        QTAILQ_INSERT_TAIL(&s->cmdq, cmd, next);
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    }

    virtio_snd_process_cmdq(s);
}

/*
 * The event virtqueue handler.
 * Not implemented yet.
 *
 * @vdev: VirtIOSound device
 * @vq: event vq
 */
static void virtio_snd_handle_event(VirtIODevice *vdev, VirtQueue *vq)
{
    qemu_log_mask(LOG_UNIMP, "virtio_snd: event queue is unimplemented.\n");
    trace_virtio_snd_handle_event();
}

/*
 * Stub buffer virtqueue handler.
 *
 * @vdev: VirtIOSound device
 * @vq: virtqueue
 */
static void virtio_snd_handle_xfer(VirtIODevice *vdev, VirtQueue *vq) {}

static uint64_t get_features(VirtIODevice *vdev, uint64_t features,
                             Error **errp)
{
    /*
     * virtio-v1.2-csd01, 5.14.3,
     * Feature Bits
     * None currently defined.
     */
    VirtIOSound *s = VIRTIO_SND(vdev);
    features |= s->features;

    trace_virtio_snd_get_features(vdev, features);

    return features;
}

static void
virtio_snd_vm_state_change(void *opaque, bool running,
                                       RunState state)
{
    if (running) {
        trace_virtio_snd_vm_state_running();
    } else {
        trace_virtio_snd_vm_state_stopped();
    }
}

static void virtio_snd_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    VirtIOSound *vsnd = VIRTIO_SND(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    virtio_snd_pcm_set_params default_params = { 0 };
    uint32_t status;

    vsnd->pcm = NULL;
    vsnd->vmstate =
        qemu_add_vm_change_state_handler(virtio_snd_vm_state_change, vsnd);

    trace_virtio_snd_realize(vsnd);

    vsnd->pcm = g_new0(VirtIOSoundPCM, 1);
    vsnd->pcm->snd = vsnd;
    vsnd->pcm->streams =
        g_new0(VirtIOSoundPCMStream *, vsnd->snd_conf.streams);
    vsnd->pcm->pcm_params =
        g_new0(virtio_snd_pcm_set_params, vsnd->snd_conf.streams);

    virtio_init(vdev, VIRTIO_ID_SOUND, sizeof(virtio_snd_config));
    virtio_add_feature(&vsnd->features, VIRTIO_F_VERSION_1);

    /* set number of jacks and streams */
    if (vsnd->snd_conf.jacks > 8) {
        error_setg(errp,
                   "Invalid number of jacks: %"PRIu32,
                   vsnd->snd_conf.jacks);
        return;
    }
    if (vsnd->snd_conf.streams < 1 || vsnd->snd_conf.streams > 10) {
        error_setg(errp,
                   "Invalid number of streams: %"PRIu32,
                    vsnd->snd_conf.streams);
        return;
    }

    if (vsnd->snd_conf.chmaps > VIRTIO_SND_CHMAP_MAX_SIZE) {
        error_setg(errp,
                   "Invalid number of channel maps: %"PRIu32,
                   vsnd->snd_conf.chmaps);
        return;
    }

    AUD_register_card("virtio-sound", &vsnd->card, errp);

    /* set default params for all streams */
    default_params.features = 0;
    default_params.buffer_bytes = cpu_to_le32(8192);
    default_params.period_bytes = cpu_to_le32(2048);
    default_params.channels = 2;
    default_params.format = VIRTIO_SND_PCM_FMT_S16;
    default_params.rate = VIRTIO_SND_PCM_RATE_48000;
    vsnd->queues[VIRTIO_SND_VQ_CONTROL] =
        virtio_add_queue(vdev, 64, virtio_snd_handle_ctrl);
    vsnd->queues[VIRTIO_SND_VQ_EVENT] =
        virtio_add_queue(vdev, 64, virtio_snd_handle_event);
    vsnd->queues[VIRTIO_SND_VQ_TX] =
        virtio_add_queue(vdev, 64, virtio_snd_handle_xfer);
    vsnd->queues[VIRTIO_SND_VQ_RX] =
        virtio_add_queue(vdev, 64, virtio_snd_handle_xfer);
    qemu_mutex_init(&vsnd->cmdq_mutex);
    QTAILQ_INIT(&vsnd->cmdq);

    for (uint32_t i = 0; i < vsnd->snd_conf.streams; i++) {
        status = virtio_snd_set_pcm_params(vsnd, i, &default_params);
        if (status != cpu_to_le32(VIRTIO_SND_S_OK)) {
            error_setg(errp,
                       "Can't initalize stream params, device responded with %s.",
                       print_code(status));
            return;
        }
        status = virtio_snd_pcm_prepare(vsnd, i);
        if (status != cpu_to_le32(VIRTIO_SND_S_OK)) {
            error_setg(errp,
                       "Can't prepare streams, device responded with %s.",
                       print_code(status));
            return;
        }
    }
}

static void virtio_snd_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSound *vsnd = VIRTIO_SND(dev);
    VirtIOSoundPCMStream *stream;

    qemu_del_vm_change_state_handler(vsnd->vmstate);
    trace_virtio_snd_unrealize(vsnd);

    if (vsnd->pcm) {
        if (vsnd->pcm->streams) {
            for (uint32_t i = 0; i < vsnd->snd_conf.streams; i++) {
                stream = vsnd->pcm->streams[i];
                if (stream) {
                    virtio_snd_process_cmdq(stream->s);
                    virtio_snd_pcm_close(stream);
                    g_free(stream);
                }
            }
            g_free(vsnd->pcm->streams);
        }
        g_free(vsnd->pcm->pcm_params);
        g_free(vsnd->pcm);
        vsnd->pcm = NULL;
    }
    AUD_remove_card(&vsnd->card);
    qemu_mutex_destroy(&vsnd->cmdq_mutex);
    virtio_delete_queue(vsnd->queues[VIRTIO_SND_VQ_CONTROL]);
    virtio_delete_queue(vsnd->queues[VIRTIO_SND_VQ_EVENT]);
    virtio_delete_queue(vsnd->queues[VIRTIO_SND_VQ_TX]);
    virtio_delete_queue(vsnd->queues[VIRTIO_SND_VQ_RX]);
    virtio_cleanup(vdev);
}


static void virtio_snd_reset(VirtIODevice *vdev)
{
    VirtIOSound *s = VIRTIO_SND(vdev);
    virtio_snd_ctrl_command *cmd;

    WITH_QEMU_LOCK_GUARD(&s->cmdq_mutex) {
        while (!QTAILQ_EMPTY(&s->cmdq)) {
            cmd = QTAILQ_FIRST(&s->cmdq);
            QTAILQ_REMOVE(&s->cmdq, cmd, next);
            virtio_snd_ctrl_cmd_free(cmd);
        }
    }
}

static void virtio_snd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);


    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    device_class_set_props(dc, virtio_snd_properties);

    dc->vmsd = &vmstate_virtio_snd;
    vdc->vmsd = &vmstate_virtio_snd_device;
    vdc->realize = virtio_snd_realize;
    vdc->unrealize = virtio_snd_unrealize;
    vdc->get_config = virtio_snd_get_config;
    vdc->set_config = virtio_snd_set_config;
    vdc->get_features = get_features;
    vdc->reset = virtio_snd_reset;
    vdc->legacy_features = 0;
}

static const TypeInfo virtio_snd_types[] = {
    {
      .name          = TYPE_VIRTIO_SND,
      .parent        = TYPE_VIRTIO_DEVICE,
      .instance_size = sizeof(VirtIOSound),
      .class_init    = virtio_snd_class_init,
    }
};

DEFINE_TYPES(virtio_snd_types)
