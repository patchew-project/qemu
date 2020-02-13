/*
 * RDMA protocol and interfaces
 *
 * Copyright IBM, Corp. 2010-2013
 * Copyright Red Hat, Inc. 2015-2016
 *
 * Authors:
 *  Michael R. Hines <mrhines@us.ibm.com>
 *  Jiuxing Liu <jl@us.ibm.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_RDMA_H
#define QEMU_MIGRATION_RDMA_H

#include "qemu-file.h"
#include "qemu-file-channel.h"

#define RDMA_RESOLVE_TIMEOUT_MS 10000

/* Do not merge data if larger than this. */
#define RDMA_MERGE_MAX (2 * 1024 * 1024)
#define RDMA_SIGNALED_SEND_MAX (RDMA_MERGE_MAX / 4096)

#define RDMA_REG_CHUNK_SHIFT 20 /* 1 MB */

/*
 * This is only for non-live state being migrated.
 * Instead of RDMA_WRITE messages, we use RDMA_SEND
 * messages for that state, which requires a different
 * delivery design than main memory.
 */
#define RDMA_SEND_INCREMENT 32768

/*
 * Maximum size infiniband SEND message
 */
#define RDMA_CONTROL_MAX_BUFFER (512 * 1024)
#define RDMA_CONTROL_MAX_COMMANDS_PER_MESSAGE 4096

#define RDMA_CONTROL_VERSION_CURRENT 1
/*
 * Capabilities for negotiation.
 */
#define RDMA_CAPABILITY_PIN_ALL 0x01

/*
 * RDMA migration protocol:
 * 1. RDMA Writes (data messages, i.e. RAM)
 * 2. IB Send/Recv (control channel messages)
 */
enum {
    RDMA_WRID_NONE = 0,
    RDMA_WRID_RDMA_WRITE = 1,
    RDMA_WRID_SEND_CONTROL = 2000,
    RDMA_WRID_RECV_CONTROL = 4000,
};

/*
 * Work request IDs for IB SEND messages only (not RDMA writes).
 * This is used by the migration protocol to transmit
 * control messages (such as device state and registration commands)
 *
 * We could use more WRs, but we have enough for now.
 */
enum {
    RDMA_WRID_READY = 0,
    RDMA_WRID_DATA,
    RDMA_WRID_CONTROL,
    RDMA_WRID_MAX,
};

/*
 * SEND/RECV IB Control Messages.
 */
enum {
    RDMA_CONTROL_NONE = 0,
    RDMA_CONTROL_ERROR,
    RDMA_CONTROL_READY,               /* ready to receive */
    RDMA_CONTROL_QEMU_FILE,           /* QEMUFile-transmitted bytes */
    RDMA_CONTROL_RAM_BLOCKS_REQUEST,  /* RAMBlock synchronization */
    RDMA_CONTROL_RAM_BLOCKS_RESULT,   /* RAMBlock synchronization */
    RDMA_CONTROL_COMPRESS,            /* page contains repeat values */
    RDMA_CONTROL_REGISTER_REQUEST,    /* dynamic page registration */
    RDMA_CONTROL_REGISTER_RESULT,     /* key to use after registration */
    RDMA_CONTROL_REGISTER_FINISHED,   /* current iteration finished */
    RDMA_CONTROL_UNREGISTER_REQUEST,  /* dynamic UN-registration */
    RDMA_CONTROL_UNREGISTER_FINISHED, /* unpinning finished */
};


/*
 * Memory and MR structures used to represent an IB Send/Recv work request.
 * This is *not* used for RDMA writes, only IB Send/Recv.
 */
typedef struct {
    uint8_t  control[RDMA_CONTROL_MAX_BUFFER]; /* actual buffer to register */
    struct   ibv_mr *control_mr;               /* registration metadata */
    size_t   control_len;                      /* length of the message */
    uint8_t *control_curr;                     /* start of unconsumed bytes */
} RDMAWorkRequestData;

/*
 * Representation of a RAMBlock from an RDMA perspective.
 * This is not transmitted, only local.
 * This and subsequent structures cannot be linked lists
 * because we're using a single IB message to transmit
 * the information. It's small anyway, so a list is overkill.
 */
typedef struct RDMALocalBlock {
    char          *block_name;
    uint8_t       *local_host_addr; /* local virtual address */
    uint64_t       remote_host_addr; /* remote virtual address */
    uint64_t       offset;
    uint64_t       length;
    struct         ibv_mr **pmr;    /* MRs for chunk-level registration */
    struct         ibv_mr *mr;      /* MR for non-chunk-level registration */
    uint32_t      *remote_keys;     /* rkeys for chunk-level registration */
    uint32_t       remote_rkey;     /* rkeys for non-chunk-level registration */
    int            index;           /* which block are we */
    unsigned int   src_index;       /* (Only used on dest) */
    bool           is_ram_block;
    int            nb_chunks;
    unsigned long *transit_bitmap;
    unsigned long *unregister_bitmap;
} RDMALocalBlock;

/*
 * Also represents a RAMblock, but only on the dest.
 * This gets transmitted by the dest during connection-time
 * to the source VM and then is used to populate the
 * corresponding RDMALocalBlock with
 * the information needed to perform the actual RDMA.
 */
typedef struct QEMU_PACKED RDMADestBlock {
    uint64_t remote_host_addr;
    uint64_t offset;
    uint64_t length;
    uint32_t remote_rkey;
    uint32_t padding;
} RDMADestBlock;

/*
 * Main structure for IB Send/Recv control messages.
 * This gets prepended at the beginning of every Send/Recv.
 */
typedef struct QEMU_PACKED {
    uint32_t len;     /* Total length of data portion */
    uint32_t type;    /* which control command to perform */
    uint32_t repeat;  /* number of commands in data portion of same type */
    uint32_t padding;
} RDMAControlHeader;

/*
 * Virtual address of the above structures used for transmitting
 * the RAMBlock descriptions at connection-time.
 * This structure is *not* transmitted.
 */
typedef struct RDMALocalBlocks {
    int nb_blocks;
    bool     init;             /* main memory init complete */
    RDMALocalBlock *block;
} RDMALocalBlocks;

/*
 * Main data structure for RDMA state.
 * While there is only one copy of this structure being allocated right now,
 * this is the place where one would start if you wanted to consider
 * having more than one RDMA connection open at the same time.
 */
typedef struct RDMAContext {
    char *host;
    int port;

    RDMAWorkRequestData wr_data[RDMA_WRID_MAX];

    /*
     * This is used by *_exchange_send() to figure out whether or not
     * the initial "READY" message has already been received or not.
     * This is because other functions may potentially poll() and detect
     * the READY message before send() does, in which case we need to
     * know if it completed.
     */
    int control_ready_expected;

    /* number of outstanding writes */
    int nb_sent;

    /*
     * store info about current buffer so that we can
     * merge it with future sends
     */
    uint64_t current_addr;
    uint64_t current_length;
    /* index of ram block the current buffer belongs to */
    int current_index;
    /* index of the chunk in the current ram block */
    int current_chunk;

    bool pin_all;

    /*
     * infiniband-specific variables for opening the device
     * and maintaining connection state and so forth.
     *
     * cm_id also has ibv_context, rdma_event_channel, and ibv_qp in
     * cm_id->verbs, cm_id->channel, and cm_id->qp.
     */
    struct rdma_cm_id *cm_id;               /* connection manager ID */
    struct rdma_cm_id *listen_id;
    bool connected;

    struct ibv_context          *verbs;
    struct rdma_event_channel   *channel;
    struct ibv_qp *qp;                      /* queue pair */
    struct ibv_comp_channel *comp_channel;  /* completion channel */
    struct ibv_pd *pd;                      /* protection domain */
    struct ibv_cq *cq;                      /* completion queue */

    /*
     * If a previous write failed (perhaps because of a failed
     * memory registration, then do not attempt any future work
     * and remember the error state.
     */
    int error_state;
    int error_reported;
    int received_error;

    /*
     * Description of ram blocks used throughout the code.
     */
    RDMALocalBlocks local_ram_blocks;
    RDMADestBlock  *dest_blocks;

    /* Index of the next RAMBlock received during block registration */
    unsigned int    next_src_index;

    /*
     * Migration on *destination* started.
     * Then use coroutine yield function.
     * Source runs in a thread, so we don't care.
     */
    int migration_started_on_destination;

    int total_registrations;
    int total_writes;

    int unregister_current, unregister_next;
    uint64_t unregistrations[RDMA_SIGNALED_SEND_MAX];

    GHashTable *blockmap;

    /* the RDMAContext for return path */
    struct RDMAContext *return_path;
    bool is_return_path;
} RDMAContext;

#define TYPE_QIO_CHANNEL_RDMA "qio-channel-rdma"
#define QIO_CHANNEL_RDMA(obj)                                     \
    OBJECT_CHECK(QIOChannelRDMA, (obj), TYPE_QIO_CHANNEL_RDMA)

typedef struct QIOChannelRDMA QIOChannelRDMA;


struct QIOChannelRDMA {
    QIOChannel parent;
    RDMAContext *rdmain;
    RDMAContext *rdmaout;
    QEMUFile *file;
    bool blocking; /* XXX we don't actually honour this yet */
};

int multifd_channel_rdma_connect(void *opaque);
int qemu_rdma_registration_handle(QEMUFile *f, void *opaque);
int qemu_rdma_exchange_send(RDMAContext *rdma, RDMAControlHeader *head,
                            uint8_t *data, RDMAControlHeader *resp,
                            int *resp_idx,
                            int (*callback)(RDMAContext *rdma));
int qemu_rdma_registration(void *opaque);

void rdma_start_outgoing_migration(void *opaque, const char *host_port,
                                   Error **errp);

void rdma_start_incoming_migration(const char *host_port, Error **errp);

#endif
