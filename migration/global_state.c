/*
 * Global State configuration
 *
 * Copyright (c) 2014-2017 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "migration.h"
#include "migration/global_state.h"
#include "migration/vmstate.h"
#include "trace.h"

typedef struct {
    RunState state_pre_migrate;
    RunState state;
    bool received;
} GlobalState;

static GlobalState global_state;

int global_state_store(void)
{
    global_state.state_pre_migrate = runstate_get();

    return 0;
}

void global_state_store_running(void)
{
    global_state.state_pre_migrate = RUN_STATE_RUNNING;
}

bool global_state_received(void)
{
    return global_state.received;
}

RunState global_state_get_runstate(void)
{
    return global_state.state;
}

static bool global_state_needed(void *opaque)
{
    GlobalState *s = opaque;

    /* If it is not optional, it is mandatory */

    if (migrate_get_current()->store_global_state) {
        return true;
    }

    /* If state is running or paused, it is not needed */

    if (s->state_pre_migrate == RUN_STATE_RUNNING ||
        s->state_pre_migrate == RUN_STATE_PAUSED) {
        return false;
    }

    /* for any other state it is needed */
    return true;
}

static int global_state_post_load(void *opaque, int version_id)
{
    GlobalState *s = opaque;
    s->received = true;
    s->state = s->state_pre_migrate;

    trace_migrate_global_state_post_load(RunState_str(s->state));
    return 0;
}

static const VMStateDescription vmstate_globalstate = {
    .name = "globalstate",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = global_state_post_load,
    .needed = global_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(state_pre_migrate, GlobalState),
        VMSTATE_END_OF_LIST()
    },
};

void register_global_state(void)
{
    /* We would use it independently that we receive it */
    global_state.received = false;
    vmstate_register(NULL, 0, &vmstate_globalstate, &global_state);
}
