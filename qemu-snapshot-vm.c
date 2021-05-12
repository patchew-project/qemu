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

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "qemu/coroutine.h"
#include "qemu/cutils.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"
#include "io/channel-buffer.h"
#include "migration/qemu-file-channel.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/ram.h"
#include "qemu-snapshot.h"

/* RAM transfer context */
typedef struct RAMCtx {
    int64_t normal_pages;       /* Total number of normal pages */
} RAMCtx;

static RAMCtx ram_ctx;

int coroutine_fn save_state_main(StateSaveCtx *s)
{
    /* TODO: implement */
    return 0;
}

int coroutine_fn load_state_main(StateLoadCtx *s)
{
    /* TODO: implement */
    return 0;
}

/* Initialize snapshot RAM state */
void ram_init_state(void)
{
    RAMCtx *ram = &ram_ctx;

    memset(ram, 0, sizeof(ram_ctx));
}

/* Destroy snapshot RAM state */
void ram_destroy_state(void)
{
    /* TODO: implement */
}
