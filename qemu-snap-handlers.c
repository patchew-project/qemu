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
#include "qemu-snap.h"

/* Save snapshot data from incoming migration stream */
int coroutine_fn snap_save_state_main(SnapSaveState *sn)
{
    /* TODO: implement */
    return 0;
}

/* Load snapshot data and send it with outgoing migration stream */
int coroutine_fn snap_load_state_main(SnapLoadState *sn)
{
    /* TODO: implement */
    return 0;
}
