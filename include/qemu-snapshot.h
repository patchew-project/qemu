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

#endif /* QEMU_SNAPSHOT_H */
