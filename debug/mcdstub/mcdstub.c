/*
 * Copyright (c) 2023 Nicolas Eder
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "sysemu/cpus.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"

#include "mcdstub/mcd_shared_defines.h"
#include "mcdstub/mcdstub.h"

typedef struct {
    CharBackend chr;
} MCDSystemState;

MCDSystemState mcdserver_system_state;

MCDState mcdserver_state;

/**
 * mcd_supports_guest_debug() - Returns true if debugging the selected
 * accelerator is supported.
 */
static bool mcd_supports_guest_debug(void)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->supports_guest_debug) {
        return ops->supports_guest_debug();
    }
    return false;
}

#ifndef _WIN32
static void mcd_sigterm_handler(int signal)
{
    if (runstate_is_running()) {
        vm_stop(RUN_STATE_PAUSED);
    }
}
#endif

/**
 * mcd_vm_state_change() - Handles a state change of the QEMU VM.
 *
 * This function is called when the QEMU VM goes through a state transition.
 * It stores the runstate the CPU is in to the cpu_state and when in
 * RUN_STATE_DEBUG it collects additional data on what watchpoint was hit.
 * This function also resets the singlestep behavior.
 * @running: True if he VM is running.
 * @state: The new (and active) VM run state.
 */
static void mcd_vm_state_change(void *opaque, bool running, RunState state)
{
}

/**
 * mcd_chr_can_receive() - Returns the maximum packet length of a TCP packet.
 */
static int mcd_chr_can_receive(void *opaque)
{
    return MAX_PACKET_LENGTH;
}

/**
 * mcd_chr_receive() - Handles receiving a TCP packet.
 *
 * This function gets called by QEMU when a TCP packet is received.
 * It iterates over that packet an calls :c:func:`mcd_read_byte` for each char
 * of the packet.
 * @buf: Content of the packet.
 * @size: Length of the packet.
 */
static void mcd_chr_receive(void *opaque, const uint8_t *buf, int size)
{
}

/**
 * mcd_chr_event() - Handles a TCP client connect.
 *
 * This function gets called by QEMU when a TCP cliet connects to the opened
 * TCP port. It attaches the first process. From here on TCP packets can be
 * exchanged.
 * @event: Type of event.
 */
static void mcd_chr_event(void *opaque, QEMUChrEvent event)
{
}

/**
 * mcd_init_mcdserver_state() - Initializes the mcdserver_state struct.
 *
 * This function allocates memory for the mcdserver_state struct and sets
 * all of its members to their inital values. This includes setting the
 * cpu_state to halted and initializing the query functions with
 * :c:func:`init_query_cmds_table`.
 */
static void mcd_init_mcdserver_state(void)
{
}

/**
 * reset_mcdserver_state() - Resets the mcdserver_state struct.
 *
 * This function deletes all processes connected to the mcdserver_state.
 */
static void reset_mcdserver_state(void)
{
}

/**
 * create_processes() - Sorts all processes and calls
 * :c:func:`mcd_create_default_process`.
 *
 * This function sorts all connected processes with the qsort function.
 * Afterwards, it creates a new process with
 * :c:func:`mcd_create_default_process`.
 * @s: A MCDState object.
 */
static void create_processes(MCDState *s)
{
}

int mcdserver_start(const char *device)
{
    char mcd_device_config[TCP_CONFIG_STRING_LENGTH];
    char mcd_tcp_port[TCP_CONFIG_STRING_LENGTH];
    Chardev *chr = NULL;

    if (!first_cpu) {
        error_report("mcdstub: meaningless to attach to a "
                     "machine without any CPU.");
        return -1;
    }

    if (!mcd_supports_guest_debug()) {
        error_report("mcdstub: current accelerator doesn't "
                     "support guest debugging");
        return -1;
    }

    if (!device) {
        return -1;
    }

    /* if device == default -> set tcp_port = tcp::<MCD_DEFAULT_TCP_PORT> */
    if (strcmp(device, "default") == 0) {
        snprintf(mcd_tcp_port, sizeof(mcd_tcp_port), "tcp::%s",
            MCD_DEFAULT_TCP_PORT);
        device = mcd_tcp_port;
    }

    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(mcd_device_config, sizeof(mcd_device_config),
                     "%s,wait=off,nodelay=on,server=on", device);
            device = mcd_device_config;
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = mcd_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
            strcpy(mcd_device_config, device);
        }
#endif
        chr = qemu_chr_new_noreplay("mcd", device, true, NULL);
        if (!chr) {
            return -1;
        }
    }

    if (!mcdserver_state.init) {
        mcd_init_mcdserver_state();

        qemu_add_vm_change_state_handler(mcd_vm_state_change, NULL);
    } else {
        qemu_chr_fe_deinit(&mcdserver_system_state.chr, true);
        reset_mcdserver_state();
    }

    create_processes(&mcdserver_state);

    if (chr) {
        qemu_chr_fe_init(&mcdserver_system_state.chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&mcdserver_system_state.chr,
                                 mcd_chr_can_receive,
                                 mcd_chr_receive, mcd_chr_event,
                                 NULL, &mcdserver_state, NULL, true);
    }
    mcdserver_state.state = chr ? RS_IDLE : RS_INACTIVE;

    return 0;
}
