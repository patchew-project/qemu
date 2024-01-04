/*
 * VIRTIO Sound Device conforming to
 *
 * "Virtual I/O Device (VIRTIO) Version 1.2
 * Committee Specification Draft 01
 * 09 May 2022"
 *
 * Copyright (c) 2023 Emmanouil Pitsidianakis <manos.pitsidianakis@linaro.org>
 * Copyright (C) 2019 OpenSynergy GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_SOUND_H
#define QEMU_VIRTIO_SOUND_H

#include "hw/virtio/virtio.h"
#include "audio/audio.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_snd.h"

#define TYPE_VIRTIO_SND "virtio-sound-device"
#define VIRTIO_SND(obj) \
        OBJECT_CHECK(VirtIOSound, (obj), TYPE_VIRTIO_SND)

/* CONFIGURATION SPACE */

typedef struct virtio_snd_config virtio_snd_config;

/* COMMON DEFINITIONS */

/* common header for request/response*/
typedef struct virtio_snd_hdr virtio_snd_hdr;

/* event notification */
typedef struct virtio_snd_event virtio_snd_event;

/* common control request to query an item information */
typedef struct virtio_snd_query_info virtio_snd_query_info;

/* JACK CONTROL MESSAGES */

typedef struct virtio_snd_jack_hdr virtio_snd_jack_hdr;

/* jack information structure */
typedef struct virtio_snd_jack_info virtio_snd_jack_info;

/* jack remapping control request */
typedef struct virtio_snd_jack_remap virtio_snd_jack_remap;

/*
 * PCM CONTROL MESSAGES
 */
typedef struct virtio_snd_pcm_hdr virtio_snd_pcm_hdr;

/* PCM stream info structure */
typedef struct virtio_snd_pcm_info virtio_snd_pcm_info;

/* set PCM stream params */
typedef struct virtio_snd_pcm_set_params virtio_snd_pcm_set_params;

/* I/O request header */
typedef struct virtio_snd_pcm_xfer virtio_snd_pcm_xfer;

/* I/O request status */
typedef struct virtio_snd_pcm_status virtio_snd_pcm_status;

/* device structs */

typedef struct VirtIOSound VirtIOSound;

typedef struct VirtIOSoundPCMStream VirtIOSoundPCMStream;

typedef struct virtio_snd_ctrl_command virtio_snd_ctrl_command;

typedef struct VirtIOSoundPCMBuffer VirtIOSoundPCMBuffer;

typedef QSIMPLEQ_HEAD(VirtIOSoundPCMBufferSimpleQueue, VirtIOSoundPCMBuffer)
    VirtIOSoundPCMBufferSimpleQueue;

/*
 * The VirtIO sound spec reuses layouts and values from the High Definition
 * Audio spec (virtio/v1.2: 5.14 Sound Device). This struct handles each I/O
 * message's buffer (virtio/v1.2: 5.14.6.8 PCM I/O Messages).
 *
 * In the case of TX (i.e. playback) buffers, we defer reading the raw PCM data
 * from the virtqueue until QEMU's sound backsystem calls the output callback.
 * This is tracked by the `bool populated;` field, which is set to true when
 * data has been read into our own buffer for consumption.
 *
 * VirtIOSoundPCMBuffer has a dynamic size since it includes the raw PCM data
 * in its allocation. It must be initialized and destroyed as follows:
 *
 *   size_t size = [[derived from owned VQ element descriptor sizes]];
 *   buffer = g_malloc0(sizeof(VirtIOSoundPCMBuffer) + size);
 *   buffer->elem = [[owned VQ element]];
 *
 *   [..]
 *
 *   g_free(buffer->elem);
 *   g_free(buffer);
 */
struct VirtIOSoundPCMBuffer {
    QSIMPLEQ_ENTRY(VirtIOSoundPCMBuffer) entry;
    VirtQueueElement *elem;
    VirtQueue *vq;
    size_t size;
    /*
     * In TX / Plaback, `offset` represents the first unused position inside
     * `data`. If `offset == size` then there are no unused data left.
     */
    uint64_t offset;
    /* Used for the TX queue for lazy I/O copy from `elem` */
    bool populated;
    /*
     * VirtIOSoundPCMBuffer is an unsized type because it ends with an array of
     * bytes. The size of `data` is determined from the I/O message's read-only
     * or write-only size when allocating VirtIOSoundPCMBuffer.
     */
    uint8_t data[];
};

struct VirtIOSoundPCMStream {
    virtio_snd_pcm_info info;
    virtio_snd_pcm_set_params params;
    uint32_t id;
    uint32_t state;
    /* placeholder: write position in current VirtIOSoundPCMBuffer */
    uint32_t buf_wpos;
    /* channel position values (VIRTIO_SND_CHMAP_XXX) */
    uint8_t positions[VIRTIO_SND_CHMAP_MAX_SIZE];
    VirtIOSound *s;
    audsettings as;
    union {
        SWVoiceIn *in;
        SWVoiceOut *out;
    } voice;
    VirtIOSoundPCMBufferSimpleQueue queue;
};

/*
 * PCM stream state machine.
 * -------------------------
 *
 * 5.14.6.6.1 PCM Command Lifecycle
 * ================================
 *
 * A PCM stream has the following command lifecycle:
 * - `SET PARAMETERS`
 *   The driver negotiates the stream parameters (format, transport, etc) with
 *   the device.
 *   Possible valid transitions: `SET PARAMETERS`, `PREPARE`.
 * - `PREPARE`
 *   The device prepares the stream (allocates resources, etc).
 *   Possible valid transitions: `SET PARAMETERS`, `PREPARE`, `START`,
 *   `RELEASE`. Output only: the driver transfers data for pre-buffing.
 * - `START`
 *   The device starts the stream (unmute, putting into running state, etc).
 *   Possible valid transitions: `STOP`.
 *   The driver transfers data to/from the stream.
 * - `STOP`
 *   The device stops the stream (mute, putting into non-running state, etc).
 *   Possible valid transitions: `START`, `RELEASE`.
 * - `RELEASE`
 *   The device releases the stream (frees resources, etc).
 *   Possible valid transitions: `SET PARAMETERS`, `PREPARE`.
 *
 * +---------------+ +---------+ +---------+ +-------+ +-------+
 * | SetParameters | | Prepare | | Release | | Start | | Stop  |
 * +---------------+ +---------+ +---------+ +-------+ +-------+
 *         |-             |           |          |         |
 *         ||             |           |          |         |
 *         |<             |           |          |         |
 *         |------------->|           |          |         |
 *         |<-------------|           |          |         |
 *         |              |-          |          |         |
 *         |              ||          |          |         |
 *         |              |<          |          |         |
 *         |              |--------------------->|         |
 *         |              |---------->|          |         |
 *         |              |           |          |-------->|
 *         |              |           |          |<--------|
 *         |              |           |<-------------------|
 *         |<-------------------------|          |         |
 *         |              |<----------|          |         |
 *
 * CTRL in the VirtIOSound device
 * ==============================
 *
 * The control messages that affect the state of a stream arrive in the
 * `virtio_snd_handle_ctrl()` queue callback and are of type `struct
 * virtio_snd_ctrl_command`. They are stored in a queue field in the device
 * type, `VirtIOSound`. This allows deferring the CTRL request completion if
 * it's not immediately possible due to locking/state reasons.
 *
 * The CTRL message is finally handled in `process_cmd()`.
 */
struct VirtIOSound {
    VirtIODevice parent_obj;

    VirtQueue *queues[VIRTIO_SND_VQ_MAX];
    VirtIOSoundPCMBufferSimpleQueue *invalidq[VIRTIO_SND_VQ_MAX];
    uint32_t queue_inuse[VIRTIO_SND_VQ_MAX];
    uint64_t features;
    VirtIOSoundPCMStream *streams;
    QEMUSoundCard card;
    VMChangeStateEntry *vmstate;
    virtio_snd_config snd_conf;
    QTAILQ_HEAD(, virtio_snd_ctrl_command) cmdq;
    VirtIOSoundPCMBufferSimpleQueue invalidq_tx, invalidq_rx;
};

struct virtio_snd_ctrl_command {
    VirtQueueElement *elem;
    VirtQueue *vq;
    virtio_snd_hdr ctrl;
    virtio_snd_hdr resp;
    size_t payload_size;
    QTAILQ_ENTRY(virtio_snd_ctrl_command) next;
};
#endif
