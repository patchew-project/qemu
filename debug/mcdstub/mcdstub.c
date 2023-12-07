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
#include "qemu/debug.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
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
 * init_query_cmds_table() - Initializes all query functions.
 *
 * This function adds all query functions to the mcd_query_cmds_table. This
 * includes their command string, handler function and parameter schema.
 * @mcd_query_cmds_table: Lookup table with all query commands.
 */
static void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table)
{}

/**
 * mcd_set_stop_cpu() - Sets c_cpu to the just stopped CPU.
 *
 * @cpu: The CPU state.
 */
static void mcd_set_stop_cpu(CPUState *cpu)
{
    mcdserver_state.c_cpu = cpu;
}

/**
 * mcd_init_debug_class() - initialize mcd-specific DebugClass
 */
static void mcd_init_debug_class(void){
    Object *obj;
    obj = object_new(TYPE_DEBUG);
    DebugState *ds = DEBUG(obj);
    DebugClass *dc = DEBUG_GET_CLASS(ds);
    dc->set_stop_cpu = mcd_set_stop_cpu;
    MachineState *ms = MACHINE(qdev_get_machine());
    ms->debug_state = ds;
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
    g_assert(!mcdserver_state.init);
    memset(&mcdserver_state, 0, sizeof(MCDState));
    mcdserver_state.init = true;
    mcdserver_state.str_buf = g_string_new(NULL);
    mcdserver_state.mem_buf = g_byte_array_sized_new(MAX_PACKET_LENGTH);
    mcdserver_state.last_packet = g_byte_array_sized_new(MAX_PACKET_LENGTH + 4);

    /*
     * What single-step modes are supported is accelerator dependent.
     * By default try to use no IRQs and no timers while single
     * stepping so as to make single stepping like a typical ICE HW step.
     */
    mcdserver_state.supported_sstep_flags =
        accel_supported_gdbstub_sstep_flags();
    mcdserver_state.sstep_flags = SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
    mcdserver_state.sstep_flags &= mcdserver_state.supported_sstep_flags;

    /* init query table */
    init_query_cmds_table(mcdserver_state.mcd_query_cmds_table);

    /* at this time the cpu hans't been started! -> set cpu_state */
    mcd_cpu_state_st cpu_state =  {
            .state = CORE_STATE_HALTED,
            .info_str = STATE_STR_INIT_HALTED,
    };
    mcdserver_state.cpu_state = cpu_state;

    /* create new debug object */
    mcd_init_debug_class();
}

/**
 * reset_mcdserver_state() - Resets the mcdserver_state struct.
 *
 * This function deletes all processes connected to the mcdserver_state.
 */
static void reset_mcdserver_state(void)
{
    g_free(mcdserver_state.processes);
    mcdserver_state.processes = NULL;
    mcdserver_state.process_num = 0;
}

/**
 * mcd_create_default_process() - Creates a default process for debugging.
 *
 * This function creates a new, not yet attached, process with an ID one above
 * the previous maximum ID.
 * @s: A MCDState object.
 */
static void mcd_create_default_process(MCDState *s)
{
    MCDProcess *process;
    int max_pid = 0;

    if (mcdserver_state.process_num) {
        max_pid = s->processes[s->process_num - 1].pid;
    }

    s->processes = g_renew(MCDProcess, s->processes, ++s->process_num);
    process = &s->processes[s->process_num - 1];

    /* We need an available PID slot for this process */
    assert(max_pid < UINT32_MAX);

    process->pid = max_pid + 1;
    process->attached = false;
}

/**
 * find_cpu_clusters() - Returns the CPU cluster of the child object.
 *
 * @param[in] child Object with unknown CPU cluster.
 * @param[in] opaque Pointer to an MCDState object.
 */
static int find_cpu_clusters(Object *child, void *opaque)
{
    if (object_dynamic_cast(child, TYPE_CPU_CLUSTER)) {
        MCDState *s = (MCDState *) opaque;
        CPUClusterState *cluster = CPU_CLUSTER(child);
        MCDProcess *process;

        s->processes = g_renew(MCDProcess, s->processes, ++s->process_num);

        process = &s->processes[s->process_num - 1];
        assert(cluster->cluster_id != UINT32_MAX);
        process->pid = cluster->cluster_id + 1;
        process->attached = false;

        return 0;
    }

    return object_child_foreach(child, find_cpu_clusters, opaque);
}

/**
 * pid_order() - Compares process IDs.
 *
 * This function returns -1 if process "a" has a ower process ID than "b".
 * If "b" has a lower ID than "a" 1 is returned and if they are qual 0 is
 * returned.
 * @a: Process a.
 * @b: Process b.
 */
static int pid_order(const void *a, const void *b)
{
    MCDProcess *pa = (MCDProcess *) a;
    MCDProcess *pb = (MCDProcess *) b;

    if (pa->pid < pb->pid) {
        return -1;
    } else if (pa->pid > pb->pid) {
        return 1;
    } else {
        return 0;
    }
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
    object_child_foreach(object_get_root(), find_cpu_clusters, s);

    if (mcdserver_state.processes) {
        /* Sort by PID */
        qsort(mcdserver_state.processes,
              mcdserver_state.process_num,
              sizeof(mcdserver_state.processes[0]),
              pid_order);
    }

    mcd_create_default_process(s);
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
