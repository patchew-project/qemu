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

#ifndef QEMU_SNAP_H
#define QEMU_SNAP_H

/* Synthetic value for invalid offset */
#define INVALID_OFFSET          ((int64_t) -1)
/* Max. byte count for QEMUFile inplace read */
#define INPLACE_READ_MAX        (32768 - 4096)

/* Target page size, if not specified explicitly in command-line */
#define DEFAULT_PAGE_SIZE       4096
/*
 * Maximum supported target page size, cause we use QEMUFile and
 * qemu_get_buffer_in_place(). IO_BUF_SIZE is currently 32KB.
 */
#define PAGE_SIZE_MAX           16384

/* Buffer size for RAM chunk loads via AIO buffer_pool */
#define AIO_BUFFER_SIZE         (1024 * 1024)
/* Max. concurrent AIO tasks */
#define AIO_TASKS_MAX           8

typedef struct AioBufferPool AioBufferPool;

typedef struct AioBufferStatus {
    /* BDRV operation start offset */
    int64_t offset;
    /* BDRV operation byte count or negative error code */
    int count;
} AioBufferStatus;

typedef struct AioBuffer {
    void *data;                 /* Data buffer */
    int size;                   /* Size of data buffer */

    AioBufferStatus status;     /* Status returned by task->func() */
} AioBuffer;

typedef struct AioBufferTask {
    AioBuffer *buffer;          /* AIO buffer */

    int64_t offset;             /* BDRV operation start offset */
    int size;                   /* BDRV requested transfer size */
} AioBufferTask;

typedef AioBufferStatus coroutine_fn (*AioBufferFunc)(AioBufferTask *task);

typedef struct QIOChannelBuffer QIOChannelBuffer;

typedef struct SnapSaveState {
    const char *filename;       /* Image file name */
    BlockBackend *blk;          /* Block backend */

    QEMUFile *f_fd;             /* Incoming migration stream QEMUFile */
    QEMUFile *f_vmstate;        /* Block backend vmstate area QEMUFile */
    /*
     * Buffer to stash first few KBs of incoming stream, part of it later will
     * go the the VMSTATE area of the image file. Specifically, these are VM
     * state header, configuration section and the section which contains
     * RAM block list.
     */
    QIOChannelBuffer *ioc_lbuf;
    /* Page coalescing buffer channel */
    QIOChannelBuffer *ioc_pbuf;

    /* BDRV offset matching start of ioc_pbuf */
    int64_t bdrv_offset;
    /* Last BDRV offset saved to ioc_pbuf */
    int64_t last_bdrv_offset;

    /* Stream read position, updated at the beginning of each new section */
    int64_t stream_pos;

    /* Stream read position at the beginning of RAM block list section */
    int64_t ram_list_pos;
    /* Stream read position at the beginning of the first RAM data section */
    int64_t ram_pos;
    /* Stream read position at the beginning of the first device state section */
    int64_t device_pos;

    /* Final status */
    int status;

    /*
     * Keep first few bytes from the beginning of each section for the case
     * when we meet device state section and go into 'default_handler'.
     */
    uint8_t section_header[512];
} SnapSaveState;

typedef struct SnapLoadState {
    BlockBackend *blk;          /* Block backend */

    QEMUFile *f_fd;             /* Outgoing migration stream QEMUFile */
    QEMUFile *f_vmstate;        /* Block backend vmstate area QEMUFile */
    /*
     * Buffer to keep first few KBs of BDRV vmstate that we stashed at the
     * start. Within this buffer are VM state header, configuration section
     * and the first 'ram' section with RAM block list.
     */
    QIOChannelBuffer *ioc_lbuf;

    /* AIO buffer pool */
    AioBufferPool *aio_pool;

    /* BDRV vmstate offset of RAM block list section */
    int64_t state_ram_list_offset;
    /* BDRV vmstate offset of the first device section */
    int64_t state_device_offset;
    /* BDRV vmstate End-Of-File */
    int64_t state_eof;
} SnapLoadState;

SnapSaveState *snap_save_get_state(void);
SnapLoadState *snap_load_get_state(void);

int coroutine_fn snap_save_state_main(SnapSaveState *sn);
int coroutine_fn snap_load_state_main(SnapLoadState *sn);

void snap_ram_init_state(int page_bits);
void snap_ram_destroy_state(void);

QEMUFile *qemu_fopen_bdrv_vmstate(BlockDriverState *bs, int is_writable);

AioBufferPool *coroutine_fn aio_pool_new(int buf_align, int buf_size, int buf_count);
void aio_pool_free(AioBufferPool *pool);
void aio_pool_set_max_in_flight(AioBufferPool *pool, int max_in_flight);
int aio_pool_status(AioBufferPool *pool);

bool coroutine_fn aio_pool_can_acquire_next(AioBufferPool *pool);
AioBuffer *coroutine_fn aio_pool_try_acquire_next(AioBufferPool *pool);
AioBuffer *coroutine_fn aio_pool_wait_compl_next(AioBufferPool *pool);
void coroutine_fn aio_buffer_release(AioBuffer *buffer);

void coroutine_fn aio_buffer_start_task(AioBuffer *buffer, AioBufferFunc func,
        int64_t offset, int size);

void file_transfer_to_eof(QEMUFile *f_dst, QEMUFile *f_src);
void file_transfer_bytes(QEMUFile *f_dst, QEMUFile *f_src, size_t size);

#endif /* QEMU_SNAP_H */
