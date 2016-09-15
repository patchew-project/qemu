/*
 * replay-snapshot.c
 *
 * Copyright (c) 2010-2016 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "qapi/qmp/qstring.h"

void replay_vmstate_init(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        QDict *opts = qdict_new();
        qdict_put(opts, "name", qstring_from_str("replay_init"));
        hmp_savevm(cur_mon, opts);
        QDECREF(opts);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        load_vmstate("replay_init");
    }
}
