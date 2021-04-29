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

/* JACK CONTROL MESSAGES */

typedef struct virtio_snd_jack_hdr {
    /* VIRTIO_SND_R_JACK_* */
    virtio_snd_hdr hdr;
    /* 0 to (virtio_snd_config.jacks - 1) */
    uint32_t jack_id;
} virtio_snd_jack_hdr;

/* Supported jack features */
enum {
    VIRTIO_SND_F_JACK_REMAP = 0
};

/* jack information structure */
typedef struct virtio_snd_jack_info {
    /* common header */
    virtio_snd_info hdr;
    /* 1 << VIRTIO_SND_JACK_F_* */
    uint32_t features;
    /* pin default configuration from HDA spec */
    uint32_t hda_reg_defconf;
    /* pin capabilities from HDA spec */
    uint32_t hda_reg_caps;
    /* connection status (0: disconnected, 1: connected) */
    uint8_t connected;

    uint8_t padding[7];
} virtio_snd_jack_info;

/* jack remapping control request */
typedef struct virtio_snd_jack_remap {
    /* .code = VIRTIO_SND_R_JACK_REMAP */
    virtio_snd_jack_hdr hdr;
    /* selected association number */
    uint32_t association;
    /* selected sequence number */
    uint32_t sequence;
} virtio_snd_jack_remap;

/* PCM CONTROL MESSAGES */

typedef struct virtio_snd_pcm_hdr {
    /* .code = VIRTIO_SND_R_PCM_* */
    virtio_snd_hdr hdr;
    /* 0 to (virtio_snd_config.streams - 1) */
    uint32_t stream_id;
} virtio_snd_pcm_hdr;


/* Supported PCM stream features */
enum {
    VIRTIO_SND_PCM_F_SHMEM_HOST = 0,
    VIRTIO_SND_PCM_F_SHMEM_GUEST,
    VIRTIO_SND_PCM_F_MSG_POLLING,
    VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS,
    VIRTIO_SNDPCM_F_EVT_XRUNS
};

/* PCM stream flags */
enum {
    VIRTIO_SND_PCM_FL_CHMAP = 0
};

/* Supported sample formats */
enum {
    /* analog formats (width / physical width) */
    VIRTIO_SND_PCM_FMT_IMA_ADPCM = 0,   /*  4 /  4 bits */
    VIRTIO_SND_PCM_FMT_MU_LAW,          /*  8 /  8 bits */
    VIRTIO_SND_PCM_FMT_A_LAW,           /*  8 /  8 bits */
    VIRTIO_SND_PCM_FMT_S8,              /*  8 /  8 bits */
    VIRTIO_SND_PCM_FMT_U8,              /*  8 /  8 bits */
    VIRTIO_SND_PCM_FMT_S16,             /* 16 / 16 bits */
    VIRTIO_SND_PCM_FMT_U16,             /* 16 / 16 bits */
    VIRTIO_SND_PCM_FMT_S18_3,           /* 18 / 24 bits */
    VIRTIO_SND_PCM_FMT_U18_3,           /* 18 / 24 bits */
    VIRTIO_SND_PCM_FMT_S20_3,           /* 20 / 24 bits */
    VIRTIO_SND_PCM_FMT_U20_3,           /* 20 / 24 bits */
    VIRTIO_SND_PCM_FMT_S24_3,           /* 24 / 24 bits */
    VIRTIO_SND_PCM_FMT_U24_3,           /* 24 / 24 bits */
    VIRTIO_SND_PCM_FMT_S20,             /* 20 / 32 bits */
    VIRTIO_SND_PCM_FMT_U20,             /* 20 / 32 bits */
    VIRTIO_SND_PCM_FMT_S24,             /* 24 / 32 bits */
    VIRTIO_SND_PCM_FMT_U24,             /* 24 / 32 bits */
    VIRTIO_SND_PCM_FMT_S32,             /* 32 / 32 bits */
    VIRTIO_SND_PCM_FMT_U32,             /* 32 / 32 bits */
    VIRTIO_SND_PCM_FMT_FLOAT,           /* 32 / 32 bits */
    VIRTIO_SND_PCM_FMT_FLOAT64,         /* 64 / 64 bits */
    /* digital formats (width / physical width) */
    VIRTIO_SND_PCM_FMT_DSD_U8,          /*  8 /  8 bits */
    VIRTIO_SND_PCM_FMT_DSD_U16,         /* 16 / 16 bits */
    VIRTIO_SND_PCM_FMT_DSD_U32,         /* 32 / 32 bits */
    VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME  /* 32 / 32 bits */
};

/* Supported PCM frame rates */
enum {
    VIRTIO_SND_PCM_RATE_5512 = 0,
    VIRTIO_SND_PCM_RATE_8000,
    VIRTIO_SND_PCM_RATE_11025,
    VIRTIO_SND_PCM_RATE_16000,
    VIRTIO_SND_PCM_RATE_22050,
    VIRTIO_SND_PCM_RATE_32000,
    VIRTIO_SND_PCM_RATE_44100,
    VIRTIO_SND_PCM_RATE_48000,
    VIRTIO_SND_PCM_RATE_64000,
    VIRTIO_SND_PCM_RATE_88200,
    VIRTIO_SND_PCM_RATE_96000,
    VIRTIO_SND_PCM_RATE_176399,
    VIRTIO_SND_PCM_RATE_192000,
    VIRTIO_SND_PCM_RATE_384000
};

/* PCM stream info structure */
typedef struct virtio_snd_pcm_info {
    /* common header */
    virtio_snd_info hdr;
    /* supported features bitmap (1 << VIRTIO_SND_PCM_F_*) */
    uint32_t features;
    /* supported sample formats bitmap (1 << VIRTIO_SND_PCM_FMT_*) */
    uint64_t formats;
    /* supported sample rates bitmap (1 << VIRTIO_SND_PCM_RATE_*) */
    uint64_t rates;
    /* direction of the stream (VIRTIO_SND_D_*) */
    uint8_t direction;
    /* min # of supported channels */
    uint8_t channels_min;
    /* max # of supported channels */
    uint8_t channels_max;

    uint8_t padding[5];
} virtio_snd_pcm_info;

/* set PCM stream params */
typedef struct virtio_snd_pcm_set_params {
    virtio_snd_pcm_hdr hdr;
    /* size of hardware buffer in bytes */
    uint32_t buffer_bytes;
    /* size of hardware period in bytes */
    uint32_t period_bytes;
    /* selected feature bitmap */
    uint32_t features;
    /* number of channels */
    uint8_t channel;
    /* VIRTIO_SND_PCM_FMT_* */
    uint8_t format;
    /* VIRTIO_SND_PCM_RATE_* */
    uint8_t rate;

    uint8_t padding;
} virtio_snd_pcm_set_params;

/* PCM I/O MESSAGES */

/* I/O request header */
typedef struct virtio_snd_pcm_xfer {
    /* 0 to (virtio_snd_config.stream - 1 */
    uint32_t stream_id;
} virtio_snd_pcm_xfer;

/* I/O request status */
typedef struct virtio_snd_pcm_status {
    /* VIRTIO_SND_S_* */
    uint32_t status;
    /* current device latency */
    uint32_t latency_bytes;
} virtio_snd_pcm_status;

/* CHANNEL MAP CONTROL MESSAGES */

typedef struct virtio_snd_chmap_hdr {
    /* .code = VIRTIO_SND_R_CHMAP_* */
    virtio_snd_hdr hdr;
    /* 0 to (virtio_snd_config.chmaps - 1) */
    uint32_t chmap_id;
} virtio_snd_chmap_hdr;

/* standard channel position definition */
enum {
    VIRTIO_SND_CHMAP_NONE = 0,  /* undefined */
    VIRTIO_SND_CHMAP_NA,        /* silent */
    VIRTIO_SND_CHMAP_MONO,      /* mono stream */
    VIRTIO_SND_CHMAP_FL,        /* front left */
    VIRTIO_SND_CHMAP_FR,        /* front right */
    VIRTIO_SND_CHMAP_RL,        /* rear left */
    VIRTIO_SND_CHMAP_RR,        /* rear right */
    VIRTIO_SND_CHMAP_FC,        /* front center */
    VIRTIO_SND_CHMAP_LFE,       /* low frequency (LFE) */
    VIRTIO_SND_CHMAP_SL,        /* side left */
    VIRTIO_SND_CHMAP_SR,        /* side right */
    VIRTIO_SND_CHMAP_RC,        /* rear center */
    VIRTIO_SND_CHMAP_FLC,       /* front left center */
    VIRTIO_SND_CHMAP_FRC,       /* front right center */
    VIRTIO_SND_CHMAP_RLC,       /* rear left center */
    VIRTIO_SND_CHMAP_RRC,       /* rear right center */
    VIRTIO_SND_CHMAP_FLW,       /* front left wide */
    VIRTIO_SND_CHMAP_FRW,       /* front right wide */
    VIRTIO_SND_CHMAP_FLH,       /* front left high */
    VIRTIO_SND_CHMAP_FCH,       /* front center high */
    VIRTIO_SND_CHMAP_FRH,       /* front right high */
    VIRTIO_SND_CHMAP_TC,        /* top center */
    VIRTIO_SND_CHMAP_TFL,       /* top front left */
    VIRTIO_SND_CHMAP_TFR,       /* top front right */
    VIRTIO_SND_CHMAP_TFC,       /* top front center */
    VIRTIO_SND_CHMAP_TRL,       /* top rear left */
    VIRTIO_SND_CHMAP_TRR,       /* top rear right */
    VIRTIO_SND_CHMAP_TRC,       /* top rear center */
    VIRTIO_SND_CHMAP_TFLC,      /* top front left center */
    VIRTIO_SND_CHMAP_TFRC,      /* top front right center */
    VIRTIO_SND_CHMAP_TSL,       /* top side left */
    VIRTIO_SND_CHMAP_TSR,       /* top side right */
    VIRTIO_SND_CHMAP_LLFE,      /* left LFE */
    VIRTIO_SND_CHMAP_RLFE,      /* right LFE */
    VIRTIO_SND_CHMAP_BC,        /* bottom center */
    VIRTIO_SND_CHMAP_BLC,       /* bottom left center */
    VIRTIO_SND_CHMAP_BRC        /* bottom right center */
};

/* maximum possible number of channels */
#define VIRTIO_SND_CHMAP_MAX_SIZE   18

typedef struct virtio_snd_chmap_info {
    /* common header */
    virtio_snd_info hdr;
    /* direction */
    uint8_t direction;
    /* # of valid channel position values */
    uint8_t channels;
    /* channel position values (VIRTIO_SND_CHMAP_*) */
    uint8_t positions[VIRTIO_SND_CHMAP_MAX_SIZE];
} virtio_snd_chmap_info;

#endif
