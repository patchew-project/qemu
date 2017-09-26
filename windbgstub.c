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
#include "exec/windbgstub.h"
#include "exec/windbgstub-utils.h"

typedef struct WindbgState {
    bool is_loaded;

    CharBackend chr;

    uint32_t ctrl_packet_id;
    uint32_t data_packet_id;
} WindbgState;

static WindbgState *windbg_state;

static int windbg_chr_can_receive(void *opaque)
{
    return PACKET_MAX_SIZE;
}

static void windbg_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    if (windbg_state->is_loaded) {
        /* T0D0: parse data */
    }
}

void windbg_try_load(void)
{
    if (windbg_state && !windbg_state->is_loaded) {
        windbg_state->is_loaded = windbg_on_load();
    }
}

static void windbg_exit(void)
{
    windbg_on_exit();
    g_free(windbg_state);
}

int windbg_server_start(const char *device)
{
    Chardev *chr = NULL;

    if (windbg_state) {
        WINDBG_ERROR("Multiple instances are not supported");
        exit(1);
    }

    windbg_state = g_new0(WindbgState, 1);
    windbg_state->ctrl_packet_id = RESET_PACKET_ID;
    windbg_state->data_packet_id = INITIAL_PACKET_ID;

    chr = qemu_chr_new_noreplay(WINDBG, device);
    if (!chr) {
        return -1;
    }

    qemu_chr_fe_init(&windbg_state->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&windbg_state->chr, windbg_chr_can_receive,
                             windbg_chr_receive, NULL, NULL, NULL, NULL, true);

    atexit(windbg_exit);
    return 0;
}
