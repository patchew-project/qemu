/*
 * QEMU External Snapshot Utility
 *
 * Copyright Virtuozzo GmbH, 2021
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#ifndef QEMU_SNAPSHOT_H
#define QEMU_SNAPSHOT_H

/* Invalid offset */
#define INVALID_OFFSET              -1
/* Maximum byte count for qemu_get_buffer_in_place() */
#define INPLACE_READ_MAX            (32768 - 4096)

/* Backing cluster size */
#define BDRV_CLUSTER_SIZE           (1024 * 1024)

/* Minimum supported target page size */
#define PAGE_SIZE_MIN               4096
/*
 * Maximum supported target page size. The limit is caused by using
 * QEMUFile and qemu_get_buffer_in_place() on migration channel.
 * IO_BUF_SIZE is currently 32KB.
 */
#define PAGE_SIZE_MAX               16384
/* RAM slice size for snapshot saving */
#define SLICE_SIZE                  PAGE_SIZE_MAX
/* RAM slice size for snapshot revert */
#define SLICE_SIZE_REVERT           (16 * PAGE_SIZE_MAX)

/* AIO transfer size */
#define AIO_TRANSFER_SIZE           BDRV_CLUSTER_SIZE
/* AIO transfer size for postcopy */
#define AIO_TRANSFER_SIZE_LOWLAT    (BDRV_CLUSTER_SIZE / 4)
/* AIO ring size */
#define AIO_RING_SIZE               64
/* AIO ring in-flight limit */
#define AIO_RING_INFLIGHT           16
/* AIO ring in-flight limit for postcopy */
#define AIO_RING_INFLIGHT_LOWLAT    4

typedef struct AioRing AioRing;

typedef struct AioRingRequest {
    void *opaque;               /* Opaque */

    void *data;                 /* Data buffer */
    int64_t offset;             /* Offset */
    size_t size;                /* Size */
} AioRingRequest;

typedef struct AioRingEvent {
    AioRingRequest *origin;     /* Originating request */
    ssize_t status;             /* Completion status */
} AioRingEvent;

typedef ssize_t coroutine_fn (*AioRingFunc)(AioRingRequest *req);

typedef struct QIOChannelBuffer QIOChannelBuffer;

typedef struct StateSaveCtx {
    BlockBackend *blk;              /* Block backend */
    QEMUFile *f_fd;                 /* QEMUFile for incoming stream */
    QEMUFile *f_vmstate;            /* QEMUFile for vmstate backing */

    QIOChannelBuffer *ioc_leader;   /* Migration stream leader */
    QIOChannelBuffer *ioc_pages;    /* Page coalescing buffer */

    /* Block offset of first page in ioc_pages */
    int64_t bdrv_offset;
    /* Block offset of the last page in ioc_pages */
    int64_t last_bdrv_offset;

    /* Current section offset */
    int64_t section_offset;
    /* Offset of the section containing list of RAM blocks */
    int64_t ram_list_offset;
    /* Offset of the first RAM section */
    int64_t ram_offset;
    /* Offset of the first non-iterable device section */
    int64_t device_offset;

    /* Zero buffer to fill unwritten slices on backing */
    void *zero_buf;

    /*
     * Since we can't rewind the state of migration stream QEMUFile, we just
     * keep first few hundreds of bytes from the beginning of each section for
     * the case if particular section appears to be the first non-iterable
     * device section and we are going to call default_handler().
     */
    uint8_t section_header[512];
} StateSaveCtx;

typedef struct StateLoadCtx {
    BlockBackend *blk;              /* Block backend */
    QEMUFile *f_fd;                 /* QEMUFile for outgoing stream */
    QEMUFile *f_rp_fd;              /* QEMUFile for return path stream */
    QEMUFile *f_vmstate;            /* QEMUFile for vmstate backing */

    QIOChannelBuffer *ioc_leader;   /* vmstate stream leader */

    AioRing *aio_ring;              /* AIO ring */

    bool postcopy;                  /* From command-line --postcopy */
    int postcopy_percent;           /* From command-line --postcopy */
    bool in_postcopy;               /* In postcopy mode */

    QemuThread rp_listen_thread;    /* Return path listening thread */
    bool has_rp_listen_thread;      /* Have listening thread */

    /* vmstate offset of the section containing list of RAM blocks */
    int64_t ram_list_offset;
    /* vmstate offset of the first non-iterable device section */
    int64_t device_offset;
    /* vmstate EOF */
    int64_t eof_offset;
} StateLoadCtx;

extern int64_t page_size;       /* Page size */
extern int64_t page_mask;       /* Page mask */
extern int page_bits;           /* Page size bits */
extern int64_t slice_size;      /* RAM slice size */
extern int64_t slice_mask;      /* RAM slice mask */
extern int slice_bits;          /* RAM slice size bits */

void ram_init_state(void);
void ram_destroy_state(void);
ssize_t coroutine_fn ram_load_aio_co(AioRingRequest *req);

StateSaveCtx *get_save_context(void);
StateLoadCtx *get_load_context(void);
int coroutine_fn save_state_main(StateSaveCtx *s);
int coroutine_fn load_state_main(StateLoadCtx *s);

AioRing *coroutine_fn aio_ring_new(AioRingFunc func, unsigned ring_entries,
        unsigned max_inflight);
void aio_ring_free(AioRing *ring);
void aio_ring_set_max_inflight(AioRing *ring, unsigned max_inflight);
AioRingRequest *coroutine_fn aio_ring_get_request(AioRing *ring);
void coroutine_fn aio_ring_submit(AioRing *ring);
AioRingEvent *coroutine_fn aio_ring_wait_event(AioRing *ring);
void coroutine_fn aio_ring_complete(AioRing *ring);

QEMUFile *qemu_fopen_bdrv_vmstate(BlockDriverState *bs, int is_writable);
void qemu_fsplice(QEMUFile *f_dst, QEMUFile *f_src, size_t size);
void qemu_fsplice_tail(QEMUFile *f_dst, QEMUFile *f_src);

#endif /* QEMU_SNAPSHOT_H */
