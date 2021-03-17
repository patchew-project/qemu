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

/* Target page size, if not specified explicitly in command-line */
#define DEFAULT_PAGE_SIZE       4096
/*
 * Maximum supported target page size, cause we use QEMUFile and
 * qemu_get_buffer_in_place(). IO_BUF_SIZE is currently 32KB.
 */
#define PAGE_SIZE_MAX           16384

typedef struct SnapSaveState {
    BlockBackend *blk;          /* Block backend */
} SnapSaveState;

typedef struct SnapLoadState {
    BlockBackend *blk;          /* Block backend */
} SnapLoadState;

SnapSaveState *snap_save_get_state(void);
SnapLoadState *snap_load_get_state(void);

int coroutine_fn snap_save_state_main(SnapSaveState *sn);
int coroutine_fn snap_load_state_main(SnapLoadState *sn);

#endif /* QEMU_SNAP_H */
