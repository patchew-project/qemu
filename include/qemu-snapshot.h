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

typedef struct StateSaveCtx {
    BlockBackend *blk;          /* Block backend */
} StateSaveCtx;

typedef struct StateLoadCtx {
    BlockBackend *blk;          /* Block backend */
} StateLoadCtx;

extern int64_t page_size;       /* Page size */
extern int64_t page_mask;       /* Page mask */
extern int page_bits;           /* Page size bits */
extern int64_t slice_size;      /* RAM slice size */
extern int64_t slice_mask;      /* RAM slice mask */
extern int slice_bits;          /* RAM slice size bits */

void ram_init_state(void);
void ram_destroy_state(void);
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
