/*
 * windbgstub.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qemu/cutils.h"
#include "exec/windbgstub.h"
#include "exec/windbgstub-utils.h"

typedef struct WindbgState {
    bool is_loaded;

    uint32_t ctrl_packet_id;
    uint32_t data_packet_id;
} WindbgState;

static WindbgState *windbg_state;

static void windbg_exit(void)
{
    g_free(windbg_state);
}

int windbg_server_start(const char *device)
{
    if (windbg_state) {
        WINDBG_ERROR("Multiple instances of windbg are not supported.");
        exit(1);
    }

    windbg_state = g_new0(WindbgState, 1);
    windbg_state->ctrl_packet_id = RESET_PACKET_ID;
    windbg_state->data_packet_id = INITIAL_PACKET_ID;

    atexit(windbg_exit);
    return 0;
}
