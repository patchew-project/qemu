/*
 * Virtio Sound Device
 */

#ifndef QEMU_VIRTIO_SOUND_H
#define QEMU_VIRTIO_SOUND_H

#include "qemu/units.h"
#include "hw/virtio/virtio.h"
#include "qemu/queue.h"
#include "audio/audio.h"
#include "audio/audio_int.h"

#define VIRTIO_ID_SOUND 25

/* CONFIGURATION SPACE */

typedef struct virtio_snd_config {
    /* # of jacks available */
    uint32_t jacks;
    /* # of streams avalable */
    uint32_t streams;
    /* # chmaps available */
    uint32_t chmaps;
} virtio_snd_config;

/* COMMON DEFINITIONS */

/* supported sample data directions. */
enum {
    VIRTIO_SND_D_OUTPUT = 0,
    VIRTIO_SND_D_INPUT
};

enum {
    /* jack control request types */
    VIRTIO_SND_R_JACK_INFO = 1,
    VIRTIO_SND_R_JACK_REMAP,

    /* PCM control request types */
    VIRTIO_SND_R_PCM_INFO = 0x0100,
    VIRTIO_SND_R_PCM_SET_PARAMS,
    VIRTIO_SND_R_PCM_PREPARE,
    VIRTIO_SND_R_PCM_RELEASE,
    VIRTIO_SND_R_PCM_START,
    VIRTIO_SND_R_PCM_STOP,

    /* channel map control request type */
    VIRTIO_SND_R_CHMAP_INFO = 0x200,

    /* jack event types */
    VIRTIO_SND_EVT_JACK_CONNECTED = 0x1000,
    VIRTIO_SND_EVT_JACK_DISCONNECTED,

    /* PCM event types */
    VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED = 0x1100,
    VIRTIO_SND_EVT_PCM_XRUN,

    /* common status codes */
    VIRTIO_SND_S_OK = 0x8000,
    VIRTIO_SND_S_BAD_MSG,
    VIRTIO_SND_S_NOT_SUPP,
    VIRTIO_SND_S_IO_ERR
};

/* common header for request/response*/
typedef struct virtio_snd_hdr {
    uint32_t code;
} virtio_snd_hdr;

/* event notification */
typedef struct virtio_snd_event {
    /* VIRTIO_SND_EVT_* */
    virtio_snd_hdr hdr;
    /* Optional event data */
    uint32_t data;
} virtio_snd_event;

/* common control request to query an item information */
typedef struct virtio_snd_query_info {
    /* VIRTIO_SND_R_*_INFO */
    struct virtio_snd_hdr hdr;
    /* item start identifier */
    uint32_t start_id;
    /* # of items to query */
    uint32_t count;
    /* size of a single item information in bytes */
    uint32_t size;
} virtio_snd_query_info;

/* common item information header */
typedef struct virtio_snd_info {
    /* functional group node id (HDA Spec 7.1.2) */
    uint32_t hda_fn_nid;
} virtio_snd_info;

#endif
