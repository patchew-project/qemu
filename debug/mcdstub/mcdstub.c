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
#include "mcdstub/arm_mcdstub.h"

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
 * mcd_get_process() - Returns the process of the provided pid.
 *
 * @pid: The process ID.
 */
static MCDProcess *mcd_get_process(uint32_t pid)
{
    int i;

    if (!pid) {
        /* 0 means any process, we take the first one */
        return &mcdserver_state.processes[0];
    }

    for (i = 0; i < mcdserver_state.process_num; i++) {
        if (mcdserver_state.processes[i].pid == pid) {
            return &mcdserver_state.processes[i];
        }
    }

    return NULL;
}

/**
 * mcd_get_cpu_pid() - Returns the process ID of the provided CPU.
 *
 * @cpu: The CPU state.
 */
static uint32_t mcd_get_cpu_pid(CPUState *cpu)
{
    if (cpu->cluster_index == UNASSIGNED_CLUSTER_INDEX) {
        /* Return the default process' PID */
        int index = mcdserver_state.process_num - 1;
        return mcdserver_state.processes[index].pid;
    }
    return cpu->cluster_index + 1;
}

/**
 * mcd_get_cpu_process() - Returns the process of the provided CPU.
 *
 * @cpu: The CPU state.
 */
static MCDProcess *mcd_get_cpu_process(CPUState *cpu)
{
    return mcd_get_process(mcd_get_cpu_pid(cpu));
}

/**
 * mcd_next_attached_cpu() - Returns the first CPU with an attached process
 * starting after the
 * provided cpu.
 *
 * @cpu: The CPU to start from.
 */
static CPUState *mcd_next_attached_cpu(CPUState *cpu)
{
    cpu = CPU_NEXT(cpu);

    while (cpu) {
        if (mcd_get_cpu_process(cpu)->attached) {
            break;
        }

        cpu = CPU_NEXT(cpu);
    }

    return cpu;
}

/**
 * mcd_first_attached_cpu() - Returns the first CPU with an attached process.
 */
static CPUState *mcd_first_attached_cpu(void)
{
    CPUState *cpu = first_cpu;
    MCDProcess *process = mcd_get_cpu_process(cpu);

    if (!process->attached) {
        return mcd_next_attached_cpu(cpu);
    }

    return cpu;
}

/**
 * mcd_get_cpu() - Returns the CPU the index i_cpu_index.
 *
 * @cpu_index: Index of the desired CPU.
 */
static CPUState *mcd_get_cpu(uint32_t cpu_index)
{
    CPUState *cpu = first_cpu;

    while (cpu) {
        if (cpu->cpu_index == cpu_index) {
            return cpu;
        }
        cpu = mcd_next_attached_cpu(cpu);
    }

    return cpu;
}

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
 * mcd_put_buffer() - Sends the buf as TCP packet with qemu_chr_fe_write_all.
 *
 * @buf: TCP packet data.
 * @len: TCP packet length.
 */
static void mcd_put_buffer(const uint8_t *buf, int len)
{
    qemu_chr_fe_write_all(&mcdserver_system_state.chr, buf, len);
}

/**
 * mcd_put_packet_binary() - Adds footer and header to the TCP packet data in
 * buf.
 *
 * Besides adding header and footer, this function also stores the complete TCP
 * packet in the last_packet member of the mcdserver_state. Then the packet
 * gets send with the :c:func:`mcd_put_buffer` function.
 * @buf: TCP packet data.
 * @len: TCP packet length.
 */
static int mcd_put_packet_binary(const char *buf, int len)
{
    g_byte_array_set_size(mcdserver_state.last_packet, 0);
    g_byte_array_append(mcdserver_state.last_packet,
        (const uint8_t *) (char[2]) { TCP_COMMAND_START, '\0' }, 1);
    g_byte_array_append(mcdserver_state.last_packet,
        (const uint8_t *) buf, len);
    g_byte_array_append(mcdserver_state.last_packet,
        (const uint8_t *) (char[2]) { TCP_COMMAND_END, '\0' }, 1);
    g_byte_array_append(mcdserver_state.last_packet,
        (const uint8_t *) (char[2]) { TCP_WAS_LAST, '\0' }, 1);

    mcd_put_buffer(mcdserver_state.last_packet->data,
        mcdserver_state.last_packet->len);
    return 0;
}

/**
 * mcd_put_packet() - Calls :c:func:`mcd_put_packet_binary` with buf and length
 * of buf.
 *
 * @buf: TCP packet data.
 */
static int mcd_put_packet(const char *buf)
{
    return mcd_put_packet_binary(buf, strlen(buf));
}

/**
 * mcd_put_strbuf() - Calls :c:func:`mcd_put_packet` with the str_buf of the
 * mcdserver_state.
 */
static void mcd_put_strbuf(void)
{
    mcd_put_packet(mcdserver_state.str_buf->str);
}

/**
 * cmd_parse_params() - Extracts all parameters from a TCP packet.
 *
 * This function uses the schema parameter to determine which type of parameter
 * to expect. It then extracts that parameter from the data and stores it in
 * the params GArray.
 * @data: TCP packet data.
 * @schema: List of expected parameters for the packet.
 * @params: GArray with all extracted parameters.
 */
static int cmd_parse_params(const char *data, const char *schema,
                            GArray *params)
{
    char data_buffer[64] = {0};
    const char *remaining_data = data;

    for (int i = 0; i < strlen(schema); i++) {
        /* get correct part of data */
        char *separator = strchr(remaining_data, ARGUMENT_SEPARATOR);

        if (separator) {
            /* multiple arguments */
            int seperator_index = (int)(separator - remaining_data);
            strncpy(data_buffer, remaining_data, seperator_index);
            data_buffer[seperator_index] = 0;
        } else {
            strncpy(data_buffer, remaining_data, strlen(remaining_data));
            data_buffer[strlen(remaining_data)] = 0;
        }

        /* store right data */
        MCDCmdVariant this_param;
        switch (schema[i]) {
        case ARG_SCHEMA_STRING:
            /* this has to be the last argument */
            this_param.data = remaining_data;
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_HEXDATA:
            g_string_printf(mcdserver_state.str_buf, "%s", data_buffer);
            break;
        case ARG_SCHEMA_INT:
            if (qemu_strtou32(remaining_data, &remaining_data, 10,
                              (uint32_t *)&this_param.data_uint32_t)) {
                                return -1;
            }
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_UINT64_T:
            if (qemu_strtou64(remaining_data, &remaining_data, 10,
                              (uint64_t *)&this_param.data_uint64_t)) {
                                return -1;
            }
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_QRYHANDLE:
            if (qemu_strtou32(remaining_data, &remaining_data, 10,
                              (uint32_t *)&this_param.query_handle)) {
                                return -1;
            }
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_CORENUM:
            if (qemu_strtou32(remaining_data, &remaining_data, 10,
                              (uint32_t *)&this_param.cpu_id)) {
                                return -1;
            }
            g_array_append_val(params, this_param);
            break;
        default:
            return -1;
        }
        /* update remaining data for the next run */
        remaining_data = &(remaining_data[1]);
    }
    return 0;
}

/**
 * process_string_cmd() - Collects all parameters from the data and calls the
 * correct handler.
 *
 * The parameters are extracted with the :c:func:`cmd_parse_params function.
 * This function selects the command in the cmds array, which fits the start of
 * the data string. This way the correct commands is selected.
 * @data: TCP packet data.
 * @cmds: Array of possible commands.
 * @num_cmds: Number of commands in the cmds array.
 */
static int process_string_cmd(void *user_ctx, const char *data,
    const MCDCmdParseEntry *cmds, int num_cmds)
{
    int i;
    g_autoptr(GArray) params = g_array_new(false, true, sizeof(MCDCmdVariant));

    if (!cmds) {
        return -1;
    }

    for (i = 0; i < num_cmds; i++) {
        const MCDCmdParseEntry *cmd = &cmds[i];
        g_assert(cmd->handler && cmd->cmd);

        /* continue if data and command are different */
        if (strncmp(data, cmd->cmd, strlen(cmd->cmd))) {
            continue;
        }

        if (strlen(cmd->schema)) {
            /* extract data for parameters */
            if (cmd_parse_params(&data[strlen(cmd->cmd)], cmd->schema, params))
            {
                return -1;
            }
        }

        /* call handler */
        cmd->handler(params, user_ctx);
        return 0;
    }

    return -1;
}

/**
 * run_cmd_parser() - Prepares the mcdserver_state before executing TCP packet
 * functions.
 *
 * This function empties the str_buf and mem_buf of the mcdserver_state and
 * then calls :c:func:`process_string_cmd`. In case this function fails, an
 * empty TCP packet is sent back the MCD Shared Library.
 * @data: TCP packet data.
 * @cmd: Handler function (can be an array of functions).
 */
static void run_cmd_parser(const char *data, const MCDCmdParseEntry *cmd)
{
    if (!data) {
        return;
    }

    g_string_set_size(mcdserver_state.str_buf, 0);
    g_byte_array_set_size(mcdserver_state.mem_buf, 0);

    if (process_string_cmd(NULL, data, cmd, 1)) {
        mcd_put_packet("");
    }
}

/**
 * init_resets() - Initializes the resets info.
 *
 * This function currently only adds all theoretical possible resets to the
 * resets GArray. None of the resets work at the moment. The resets are:
 * "full_system_reset", "gpr_reset" and "memory_reset".
 * @resets: GArray with possible resets.
 */
static int init_resets(GArray *resets)
{
    mcd_reset_st system_reset = { .id = 0, .name = RESET_SYSTEM};
    mcd_reset_st gpr_reset = { .id = 1, .name = RESET_GPR};
    mcd_reset_st memory_reset = { .id = 2, .name = RESET_MEMORY};
    g_array_append_vals(resets, (gconstpointer)&system_reset, 1);
    g_array_append_vals(resets, (gconstpointer)&gpr_reset, 1);
    g_array_append_vals(resets, (gconstpointer)&memory_reset, 1);
    return 0;
}

/**
 * init_trigger() - Initializes the trigger info.
 *
 * This function adds the types of trigger, their possible options and actions
 * to the trigger struct.
 * @trigger: Struct with all trigger info.
 */
static int init_trigger(mcd_trigger_into_st *trigger)
{
    snprintf(trigger->type, sizeof(trigger->type),
        "%d,%d,%d,%d", MCD_BREAKPOINT_HW, MCD_BREAKPOINT_READ,
        MCD_BREAKPOINT_WRITE, MCD_BREAKPOINT_RW);
    snprintf(trigger->option, sizeof(trigger->option),
        "%s", MCD_TRIG_OPT_VALUE);
    snprintf(trigger->action, sizeof(trigger->action),
        "%s", MCD_TRIG_ACT_BREAK);
    /* there can be 16 breakpoints and 16 watchpoints each */
    trigger->nr_trigger = 16;
    return 0;
}

/**
 * handle_open_server() - Handler for opening the MCD server.
 *
 * This is the first function that gets called from the MCD Shared Library.
 * It initializes core indepent data with the :c:func:`init_resets` and
 * \reg init_trigger functions. It also send the TCP_HANDSHAKE_SUCCESS
 * packet back to the library to confirm the mcdstub is ready for further
 * communication.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_open_server(GArray *params, void *user_ctx)
{
    /* initialize core-independent data */
    int return_value = 0;
    mcdserver_state.resets = g_array_new(false, true, sizeof(mcd_reset_st));
    return_value = init_resets(mcdserver_state.resets);
    if (return_value != 0) {
        g_assert_not_reached();
    }
    return_value = init_trigger(&mcdserver_state.trigger);
    if (return_value != 0) {
        g_assert_not_reached();
    }

    mcd_put_packet(TCP_HANDSHAKE_SUCCESS);
}

/**
 * mcd_vm_start() - Starts all CPUs with the vm_start function.
 */
static void mcd_vm_start(void)
{
    if (!runstate_needs_reset() && !runstate_is_running()) {
        vm_start();
    }
}

/**
 * handle_close_server() - Handler for closing the MCD server.
 *
 * This function detaches the debugger (process) and frees up memory.
 * Then it start the QEMU VM with :c:func:`mcd_vm_start`.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_close_server(GArray *params, void *user_ctx)
{
    uint32_t pid = 1;
    MCDProcess *process = mcd_get_process(pid);

    /*
     * 1. free memory
     * TODO: do this only if there are no processes attached anymore!
     */
    g_list_free(mcdserver_state.all_memspaces);
    g_list_free(mcdserver_state.all_reggroups);
    g_list_free(mcdserver_state.all_registers);
    g_array_free(mcdserver_state.resets, TRUE);

    /* 2. detach */
    process->attached = false;

    /* 3. reset process */
    if (pid == mcd_get_cpu_pid(mcdserver_state.c_cpu)) {
        mcdserver_state.c_cpu = mcd_first_attached_cpu();
    }
    if (!mcdserver_state.c_cpu) {
        /* no more processes attached */
        mcd_vm_start();
    }
}

/**
 * handle_gen_query() - Handler for all TCP query packets.
 *
 * Calls :c:func:`process_string_cmd` with all query functions in the
 * mcd_query_cmds_table. :c:func:`process_string_cmd` then selects the correct
 * one. This function just passes on the TCP packet data string from the
 * parameters.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_gen_query(GArray *params, void *user_ctx)
{
    if (!params->len) {
        return;
    }
    /* iterate over all possible query functions and execute the right one */
    if (process_string_cmd(NULL, get_param(params, 0)->data,
                           mcdserver_state.mcd_query_cmds_table,
                           ARRAY_SIZE(mcdserver_state.mcd_query_cmds_table))) {
        mcd_put_packet("");
    }
}

/**
 * handle_open_core() - Handler for opening a core.
 *
 * This function initializes all data for the core with the ID provided in
 * the first parameter. In has a swtich case for different architectures.
 * Currently only 32-Bit ARM is supported. The data includes memory spaces,
 * register groups and registers themselves. They get stored into GLists where
 * every entry in the list corresponds to one opened core.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_open_core(GArray *params, void *user_ctx)
{
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    CPUState *cpu = mcd_get_cpu(cpu_id);
    mcdserver_state.c_cpu = cpu;
    CPUClass *cc = CPU_GET_CLASS(cpu);
    const gchar *arch = cc->gdb_arch_name(cpu);
    int return_value = 0;

    /* prepare data strucutures */
    GArray *memspaces = g_array_new(false, true, sizeof(mcd_mem_space_st));
    GArray *reggroups = g_array_new(false, true, sizeof(mcd_reg_group_st));
    GArray *registers = g_array_new(false, true, sizeof(mcd_reg_st));

    if (strcmp(arch, MCDSTUB_ARCH_ARM) == 0) {
        /* TODO: make group and memspace ids dynamic */
        int current_group_id = 1;
        /* 1. store mem spaces */
        return_value = arm_mcd_store_mem_spaces(cpu, memspaces);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 2. parse core xml */
        return_value = arm_mcd_parse_core_xml_file(cc, reggroups,
            registers, &current_group_id);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 3. parse other xmls */
        return_value = arm_mcd_parse_general_xml_files(cpu, reggroups,
            registers, &current_group_id);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 4. add additional data the the regs from the xmls */
        return_value = arm_mcd_get_additional_register_info(reggroups,
            registers, cpu);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 5. store all found data */
        if (g_list_nth(mcdserver_state.all_memspaces, cpu_id)) {
            GList *memspaces_ptr =
                g_list_nth(mcdserver_state.all_memspaces, cpu_id);
            memspaces_ptr->data = memspaces;
        } else {
            mcdserver_state.all_memspaces =
                g_list_insert(mcdserver_state.all_memspaces, memspaces, cpu_id);
        }
        if (g_list_nth(mcdserver_state.all_reggroups, cpu_id)) {
            GList *reggroups_ptr =
                g_list_nth(mcdserver_state.all_reggroups, cpu_id);
            reggroups_ptr->data = reggroups;
        } else {
            mcdserver_state.all_reggroups =
                g_list_insert(mcdserver_state.all_reggroups, reggroups, cpu_id);
        }
        if (g_list_nth(mcdserver_state.all_registers, cpu_id)) {
            GList *registers_ptr =
                g_list_nth(mcdserver_state.all_registers, cpu_id);
            registers_ptr->data = registers;
        } else {
            mcdserver_state.all_registers =
                g_list_insert(mcdserver_state.all_registers, registers, cpu_id);
        }
    } else {
        /* we don't support other architectures */
        g_assert_not_reached();
    }
}

/**
 * handle_close_core() - Handler for closing a core.
 *
 * Frees all memory allocated for core specific information. This includes
 * memory spaces, register groups and registers.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_close_core(GArray *params, void *user_ctx)
{
    /* free memory for correct core */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    GArray *memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);
    mcdserver_state.all_memspaces =
        g_list_remove(mcdserver_state.all_memspaces, memspaces);
    g_array_free(memspaces, TRUE);
    GArray *reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);
    mcdserver_state.all_reggroups =
        g_list_remove(mcdserver_state.all_reggroups, reggroups);
    g_array_free(reggroups, TRUE);
    GArray *registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);
    mcdserver_state.all_registers =
        g_list_remove(mcdserver_state.all_registers, registers);
    g_array_free(registers, TRUE);
}

/**
 * mcd_handle_packet() - Evaluates the type of received packet and chooses the
 * correct handler.
 *
 * This function takes the first character of the line_buf to determine the
 * type of packet. Then it selects the correct handler function and parameter
 * schema. With this info it calls :c:func:`run_cmd_parser`.
 * @line_buf: TCP packet data.
 */
static int mcd_handle_packet(const char *line_buf)
{
    /*
     * decides what function (handler) to call depending on
     * the first character in the line_buf
     */
    const MCDCmdParseEntry *cmd_parser = NULL;

    switch (line_buf[0]) {
    case TCP_CHAR_OPEN_SERVER:
        {
            static MCDCmdParseEntry open_server_cmd_desc = {
                .handler = handle_open_server,
            };
            open_server_cmd_desc.cmd = (char[2]) { TCP_CHAR_OPEN_SERVER, '\0' };
            cmd_parser = &open_server_cmd_desc;
        }
        break;
    case TCP_CHAR_CLOSE_SERVER:
        {
            static MCDCmdParseEntry close_server_cmd_desc = {
                .handler = handle_close_server,
            };
            close_server_cmd_desc.cmd =
                (char[2]) { TCP_CHAR_CLOSE_SERVER, '\0' };
            cmd_parser = &close_server_cmd_desc;
        }
        break;
    case TCP_CHAR_QUERY:
        {
            static MCDCmdParseEntry query_cmd_desc = {
                .handler = handle_gen_query,
            };
            query_cmd_desc.cmd = (char[2]) { TCP_CHAR_QUERY, '\0' };
            strcpy(query_cmd_desc.schema,
                (char[2]) { ARG_SCHEMA_STRING, '\0' });
            cmd_parser = &query_cmd_desc;
        }
        break;
    case TCP_CHAR_OPEN_CORE:
        {
            static MCDCmdParseEntry open_core_cmd_desc = {
                .handler = handle_open_core,
            };
            open_core_cmd_desc.cmd = (char[2]) { TCP_CHAR_OPEN_CORE, '\0' };
            strcpy(open_core_cmd_desc.schema,
                (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
            cmd_parser = &open_core_cmd_desc;
        }
        break;
    case TCP_CHAR_CLOSE_CORE:
        {
            static MCDCmdParseEntry close_core_cmd_desc = {
                .handler = handle_close_core,
            };
            close_core_cmd_desc.cmd = (char[2]) { TCP_CHAR_CLOSE_CORE, '\0' };
            strcpy(close_core_cmd_desc.schema,
                (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
            cmd_parser = &close_core_cmd_desc;
        }
        break;
    default:
        /* command not supported */
        mcd_put_packet("");
        break;
    }

    if (cmd_parser) {
        /* parse commands and run the selected handler function */
        run_cmd_parser(line_buf, cmd_parser);
    }

    return RS_IDLE;
}

/**
 * mcd_read_byte() - Resends the last packet if not acknowledged and extracts
 * the data from a received TCP packet.
 *
 * In case the last sent packet was not acknowledged from the mcdstub,
 * this function resends it.
 * If it was acknowledged this function parses the incoming packet
 * byte by byte.
 * It extracts the data in the packet and sends an
 * acknowledging response when finished. Then :c:func:`mcd_handle_packet` gets
 * called.
 * @ch: Character of the received TCP packet, which should be parsed.
 */
static void mcd_read_byte(uint8_t ch)
{
     uint8_t reply;

    if (mcdserver_state.last_packet->len) {
        if (ch == TCP_NOT_ACKNOWLEDGED) {
            /* the previous packet was not akcnowledged */
            mcd_put_buffer(mcdserver_state.last_packet->data,
                mcdserver_state.last_packet->len);
        } else if (ch == TCP_ACKNOWLEDGED) {
            /* the previous packet was acknowledged */
        }

        if (ch == TCP_ACKNOWLEDGED || ch == TCP_COMMAND_START) {
            /*
             * either acknowledged or a new communication starts
             * -> discard previous packet
             */
            g_byte_array_set_size(mcdserver_state.last_packet, 0);
        }
        if (ch != TCP_COMMAND_START) {
            /* skip to the next char */
            return;
        }
    }

    switch (mcdserver_state.state) {
    case RS_IDLE:
        if (ch == TCP_COMMAND_START) {
            /* start of command packet */
            mcdserver_state.line_buf_index = 0;
            mcdserver_state.line_sum = 0;
            mcdserver_state.state = RS_GETLINE;
        }
        break;
    case RS_GETLINE:
        if (ch == TCP_COMMAND_END) {
            /* end of command */
            mcdserver_state.line_buf[mcdserver_state.line_buf_index++] = 0;
            mcdserver_state.state = RS_DATAEND;
        } else if (mcdserver_state.line_buf_index >=
            sizeof(mcdserver_state.line_buf) - 1) {
            /* the input string is too long for the linebuffer! */
            mcdserver_state.state = RS_IDLE;
        } else {
            /* copy the content to the line_buf */
            mcdserver_state.line_buf[mcdserver_state.line_buf_index++] = ch;
            mcdserver_state.line_sum += ch;
        }
        break;
    case RS_DATAEND:
        if (ch == TCP_WAS_NOT_LAST) {
            reply = TCP_ACKNOWLEDGED;
            mcd_put_buffer(&reply, 1);
            mcdserver_state.state = mcd_handle_packet(mcdserver_state.line_buf);
        } else if (ch == TCP_WAS_LAST) {
            reply = TCP_ACKNOWLEDGED;
            mcd_put_buffer(&reply, 1);
            mcdserver_state.state = mcd_handle_packet(mcdserver_state.line_buf);
        } else {
            /* not acknowledged! */
            reply = TCP_NOT_ACKNOWLEDGED;
            mcd_put_buffer(&reply, 1);
            /* waiting for package to get resent */
            mcdserver_state.state = RS_IDLE;
        }
        break;
    default:
        abort();
    }
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
    int i;

    for (i = 0; i < size; i++) {
        mcd_read_byte(buf[i]);
        if (buf[i] == 0) {
            break;
        }
    }
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
    int i;
    MCDState *s = (MCDState *) opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        /* Start with first process attached, others detached */
        for (i = 0; i < s->process_num; i++) {
            s->processes[i].attached = !i;
        }

        s->c_cpu = mcd_first_attached_cpu();
        break;
    default:
        break;
    }
}

/**
 * handle_query_system() - Handler for the system query.
 *
 * Sends the system name, which is "qemu-system".
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_system(GArray *params, void *user_ctx)
{
    mcd_put_packet(MCD_SYSTEM_NAME);
}

/**
 * handle_query_cores() - Handler for the core query.
 *
 * This function sends the type of core and number of cores currently
 * simulated by QEMU. It also sends a device name for the MCD data structure.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_cores(GArray *params, void *user_ctx)
{
    /* get first cpu */
    CPUState *cpu = mcd_first_attached_cpu();
    if (!cpu) {
        return;
    }

    ObjectClass *oc = object_get_class(OBJECT(cpu));
    const char *cpu_model = object_class_get_name(oc);

    CPUClass *cc = CPU_GET_CLASS(cpu);
    const gchar *arch = cc->gdb_arch_name(cpu);

    uint32_t nr_cores = cpu->nr_cores;
    char device_name[ARGUMENT_STRING_LENGTH] = {0};
    if (arch) {
        snprintf(device_name, sizeof(device_name), "qemu-%s-device", arch);
    }
    g_string_printf(mcdserver_state.str_buf, "%s=%s.%s=%s.%s=%u.",
        TCP_ARGUMENT_DEVICE, device_name, TCP_ARGUMENT_CORE, cpu_model,
        TCP_ARGUMENT_AMOUNT_CORE, nr_cores);
    mcd_put_strbuf();
}

/**
 * handle_query_reset_f() - Handler for the first reset query.
 *
 * This function sends the first reset name and ID.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_reset_f(GArray *params, void *user_ctx)
{
    /* 1. check length */
    int nb_resets = mcdserver_state.resets->len;
    if (nb_resets == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "1%s", QUERY_END_INDEX);
    }
    /* 2. send data */
    mcd_reset_st reset = g_array_index(mcdserver_state.resets, mcd_reset_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%u.",
        TCP_ARGUMENT_NAME, reset.name, TCP_ARGUMENT_ID, reset.id);
    mcd_put_strbuf();
}

/**
 * handle_query_reset_c() - Handler for all consecutive reset queries.
 *
 * This functions sends all consecutive reset names and IDs. It uses the
 * query_index parameter to determine which reset is queried next.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_reset_c(GArray *params, void *user_ctx)
{
    /* reset options are the same for every cpu! */
    uint32_t query_index = get_param(params, 0)->query_handle;

    /* 1. check weather this was the last mem space */
    int nb_groups = mcdserver_state.resets->len;
    if (query_index + 1 == nb_groups) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "%u!", query_index + 1);
    }

    /* 2. send data */
    mcd_reset_st reset = g_array_index(mcdserver_state.resets,
        mcd_reset_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%u.",
        TCP_ARGUMENT_NAME, reset.name, TCP_ARGUMENT_ID, reset.id);
    mcd_put_strbuf();
}

/**
 * handle_query_trigger() - Handler for trigger query.
 *
 * Sends data on the different types of trigger and their options and actions.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_trigger(GArray *params, void *user_ctx)
{
    mcd_trigger_into_st trigger = mcdserver_state.trigger;
    g_string_printf(mcdserver_state.str_buf, "%s=%u.%s=%s.%s=%s.%s=%s.",
        TCP_ARGUMENT_AMOUNT_TRIGGER, trigger.nr_trigger,
        TCP_ARGUMENT_TYPE, trigger.type,
        TCP_ARGUMENT_OPTION, trigger.option,
        TCP_ARGUMENT_ACTION, trigger.action);
    mcd_put_strbuf();
}

/**
 * handle_query_mem_spaces_f() Handler for the first memory space query.
 *
 * This function sends the first memory space name, ID, type and accessing
 * options.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_mem_spaces_f(GArray *params, void *user_ctx)
{
    /* 1. get correct memspaces and set the query_cpu */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray *memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);

    /* 2. check length */
    int nb_groups = memspaces->len;
    if (nb_groups == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "1%s", QUERY_END_INDEX);
    }

    /* 3. send data */
    mcd_mem_space_st space = g_array_index(memspaces, mcd_mem_space_st, 0);
    g_string_append_printf(mcdserver_state.str_buf,
        "%s=%s.%s=%u.%s=%u.%s=%u.%s=%u.%s=%u.%s=%ld.%s=%ld.%s=%u.",
        TCP_ARGUMENT_NAME, space.name,
        TCP_ARGUMENT_ID, space.id,
        TCP_ARGUMENT_TYPE, space.type,
        TCP_ARGUMENT_BITS_PER_MAU, space.bits_per_mau,
        TCP_ARGUMENT_INVARIANCE, space.invariance,
        TCP_ARGUMENT_ENDIAN, space.endian,
        TCP_ARGUMENT_MIN, space.min_addr,
        TCP_ARGUMENT_MAX, space.max_addr,
        TCP_ARGUMENT_SUPPORTED_ACCESS_OPTIONS, space.supported_access_options);
    mcd_put_strbuf();
}

/**
 * handle_query_mem_spaces_c() - Handler for all consecutive memory space
 * queries.
 *
 * This function sends all consecutive memory space names, IDs, types and
 * accessing options.
 * It uses the query_index parameter to determine
 * which memory space is queried next.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_mem_spaces_c(GArray *params, void *user_ctx)
{
    /*
     * this funcitons send all mem spaces except for the first
     * 1. get parameter and memspace
     */
    uint32_t query_index = get_param(params, 0)->query_handle;
    uint32_t cpu_id = mcdserver_state.query_cpu_id;
    GArray *memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);

    /* 2. check weather this was the last mem space */
    int nb_groups = memspaces->len;
    if (query_index + 1 == nb_groups) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "%u!", query_index + 1);
    }

    /* 3. send the correct memspace */
    mcd_mem_space_st space = g_array_index(memspaces,
        mcd_mem_space_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf,
        "%s=%s.%s=%u.%s=%u.%s=%u.%s=%u.%s=%u.%s=%ld.%s=%ld.%s=%u.",
        TCP_ARGUMENT_NAME, space.name,
        TCP_ARGUMENT_ID, space.id,
        TCP_ARGUMENT_TYPE, space.type,
        TCP_ARGUMENT_BITS_PER_MAU, space.bits_per_mau,
        TCP_ARGUMENT_INVARIANCE, space.invariance,
        TCP_ARGUMENT_ENDIAN, space.endian,
        TCP_ARGUMENT_MIN, space.min_addr,
        TCP_ARGUMENT_MAX, space.max_addr,
        TCP_ARGUMENT_SUPPORTED_ACCESS_OPTIONS, space.supported_access_options);
    mcd_put_strbuf();
}

/**
 * handle_query_reg_groups_f() - Handler for the first register group query.
 *
 * This function sends the first register group name and ID.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_reg_groups_f(GArray *params, void *user_ctx)
{
    /* 1. get correct reggroups and set the query_cpu */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray *reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);

    /* 2. check length */
    int nb_groups = reggroups->len;
    if (nb_groups == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "1%s", QUERY_END_INDEX);
    }
    /* 3. send data */
    mcd_reg_group_st group = g_array_index(reggroups, mcd_reg_group_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%u.%s=%s.",
        TCP_ARGUMENT_ID, group.id, TCP_ARGUMENT_NAME, group.name);
    mcd_put_strbuf();
}

/**
 * handle_query_reg_groups_c() - Handler for all consecutive register group
 * queries.
 *
 * This function sends all consecutive register group names and IDs. It uses
 * the query_index parameter to determine which register group is queried next.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_reg_groups_c(GArray *params, void *user_ctx)
{
    /*
     * this funcitons send all reg groups except for the first
     * 1. get parameter and memspace
     */
    uint32_t query_index = get_param(params, 0)->query_handle;
    uint32_t cpu_id = mcdserver_state.query_cpu_id;
    GArray *reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);

    /* 2. check weather this was the last reg group */
    int nb_groups = reggroups->len;
    if (query_index + 1 == nb_groups) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "%u!", query_index + 1);
    }

    /* 3. send the correct reggroup */
    mcd_reg_group_st group = g_array_index(reggroups, mcd_reg_group_st,
        query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%u.%s=%s.",
        TCP_ARGUMENT_ID, group.id, TCP_ARGUMENT_NAME, group.name);
    mcd_put_strbuf();
}

/**
 * handle_query_regs_f() - Handler for the first register query.
 *
 * This function sends the first register with all its information.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_regs_f(GArray *params, void *user_ctx)
{
    /* 1. get correct registers and set the query_cpu */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray *registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);

    /* 2. check length */
    int nb_regs = registers->len;
    if (nb_regs == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "1%s", QUERY_END_INDEX);
    }
    /* 3. send data */
    mcd_reg_st my_register = g_array_index(registers, mcd_reg_st, 0);
    g_string_append_printf(mcdserver_state.str_buf,
        "%s=%u.%s=%s.%s=%u.%s=%u.%s=%u.%s=%u.%s=%u.%s=%u.",
        TCP_ARGUMENT_ID, my_register.id,
        TCP_ARGUMENT_NAME, my_register.name,
        TCP_ARGUMENT_SIZE, my_register.bitsize,
        TCP_ARGUMENT_REGGROUPID, my_register.mcd_reg_group_id,
        TCP_ARGUMENT_MEMSPACEID, my_register.mcd_mem_space_id,
        TCP_ARGUMENT_TYPE, my_register.mcd_reg_type,
        TCP_ARGUMENT_THREAD, my_register.mcd_hw_thread_id,
        TCP_ARGUMENT_OPCODE, my_register.opcode);
    mcd_put_strbuf();
}

/**
 * handle_query_regs_c() - Handler for all consecutive register queries.
 *
 * This function sends all consecutive registers with all their information.
 * It uses the query_index parameter to determine
 * which register is queried next.
 * @params: GArray with all TCP packet parameters.
 */
static void handle_query_regs_c(GArray *params, void *user_ctx)
{
    /*
     * this funcitons send all regs except for the first
     * 1. get parameter and registers
     */
    uint32_t query_index = get_param(params, 0)->query_handle;
    uint32_t cpu_id = mcdserver_state.query_cpu_id;
    GArray *registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);

    /* 2. check weather this was the last register */
    int nb_regs = registers->len;
    if (query_index + 1 == nb_regs) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0%s", QUERY_END_INDEX);
    } else {
        g_string_printf(mcdserver_state.str_buf, "%u!", query_index + 1);
    }

    /* 3. send the correct register */
    mcd_reg_st my_register = g_array_index(registers, mcd_reg_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf,
        "%s=%u.%s=%s.%s=%u.%s=%u.%s=%u.%s=%u.%s=%u.%s=%u.",
        TCP_ARGUMENT_ID, my_register.id,
        TCP_ARGUMENT_NAME, my_register.name,
        TCP_ARGUMENT_SIZE, my_register.bitsize,
        TCP_ARGUMENT_REGGROUPID, my_register.mcd_reg_group_id,
        TCP_ARGUMENT_MEMSPACEID, my_register.mcd_mem_space_id,
        TCP_ARGUMENT_TYPE, my_register.mcd_reg_type,
        TCP_ARGUMENT_THREAD, my_register.mcd_hw_thread_id,
        TCP_ARGUMENT_OPCODE, my_register.opcode);
    mcd_put_strbuf();
}

/**
 * init_query_cmds_table() - Initializes all query functions.
 *
 * This function adds all query functions to the mcd_query_cmds_table. This
 * includes their command string, handler function and parameter schema.
 * @mcd_query_cmds_table: Lookup table with all query commands.
 */
static void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table)
{
    /* initalizes a list of all query commands */
    int cmd_number = 0;

    MCDCmdParseEntry query_system = {
        .handler = handle_query_system,
        .cmd = QUERY_ARG_SYSTEM,
    };
    mcd_query_cmds_table[cmd_number] = query_system;
    cmd_number++;

    MCDCmdParseEntry query_cores = {
        .handler = handle_query_cores,
        .cmd = QUERY_ARG_CORES,
    };
    mcd_query_cmds_table[cmd_number] = query_cores;
    cmd_number++;

    MCDCmdParseEntry query_reset_f = {
        .handler = handle_query_reset_f,
        .cmd = QUERY_ARG_RESET QUERY_FIRST,
    };
    mcd_query_cmds_table[cmd_number] = query_reset_f;
    cmd_number++;

    MCDCmdParseEntry query_reset_c = {
        .handler = handle_query_reset_c,
        .cmd = QUERY_ARG_RESET QUERY_CONSEQUTIVE,
    };
    strcpy(query_reset_c.schema, (char[2]) { ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_reset_c;
    cmd_number++;

    MCDCmdParseEntry query_trigger = {
        .handler = handle_query_trigger,
        .cmd = QUERY_ARG_TRIGGER,
    };
    mcd_query_cmds_table[cmd_number] = query_trigger;
    cmd_number++;

    MCDCmdParseEntry query_mem_spaces_f = {
        .handler = handle_query_mem_spaces_f,
        .cmd = QUERY_ARG_MEMORY QUERY_FIRST,
    };
    strcpy(query_mem_spaces_f.schema, (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_mem_spaces_f;
    cmd_number++;

    MCDCmdParseEntry query_mem_spaces_c = {
        .handler = handle_query_mem_spaces_c,
        .cmd = QUERY_ARG_MEMORY QUERY_CONSEQUTIVE,
    };
    strcpy(query_mem_spaces_c.schema, (char[2]) { ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_mem_spaces_c;
    cmd_number++;

    MCDCmdParseEntry query_reg_groups_f = {
        .handler = handle_query_reg_groups_f,
        .cmd = QUERY_ARG_REGGROUP QUERY_FIRST,
    };
    strcpy(query_reg_groups_f.schema, (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_reg_groups_f;
    cmd_number++;

    MCDCmdParseEntry query_reg_groups_c = {
        .handler = handle_query_reg_groups_c,
        .cmd = QUERY_ARG_REGGROUP QUERY_CONSEQUTIVE,
    };
    strcpy(query_reg_groups_c.schema, (char[2]) { ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_reg_groups_c;
    cmd_number++;

    MCDCmdParseEntry query_regs_f = {
        .handler = handle_query_regs_f,
        .cmd = QUERY_ARG_REG QUERY_FIRST,
    };
    strcpy(query_regs_f.schema, (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_regs_f;
    cmd_number++;

    MCDCmdParseEntry query_regs_c = {
        .handler = handle_query_regs_c,
        .cmd = QUERY_ARG_REG QUERY_CONSEQUTIVE,
    };
    strcpy(query_regs_c.schema, (char[2]) { ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_regs_c;
    cmd_number++;
}

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

void parse_reg_xml(const char *xml, int size, GArray* registers,
    uint8_t reg_type, uint32_t reg_id_offset)
{
    /* iterates over the complete xml file */
    int i, j;
    uint32_t current_reg_id = reg_id_offset;
    uint32_t internal_id;
    int still_to_skip = 0;
    char argument[64] = {0};
    char value[64] = {0};
    bool is_reg = false;
    bool is_argument = false;
    bool is_value = false;
    GArray *reg_data;

    char c;
    char *c_ptr;

    xml_attrib attribute_j;
    const char *argument_j;
    const char *value_j;

    for (i = 0; i < size; i++) {
        c = xml[i];
        c_ptr = &c;

        if (still_to_skip > 0) {
            /* skip unwanted chars */
            still_to_skip--;
            continue;
        }

        if (strncmp(&xml[i], "<reg", 4) == 0) {
            /* start of a register */
            still_to_skip = 3;
            is_reg = true;
            reg_data = g_array_new(false, true, sizeof(xml_attrib));
        } else if (is_reg) {
            if (strncmp(&xml[i], "/>", 2) == 0) {
                /* end of register info */
                still_to_skip = 1;
                is_reg = false;

                /* create empty register */
                mcd_reg_st my_register = (const struct mcd_reg_st){ 0 };

                /* add found attribtues */
                for (j = 0; j < reg_data->len; j++) {
                    attribute_j = g_array_index(reg_data, xml_attrib, j);

                    argument_j = attribute_j.argument;
                    value_j = attribute_j.value;

                    if (strcmp(argument_j, "name") == 0) {
                        strcpy(my_register.name, value_j);
                    } else if (strcmp(argument_j, "regnum") == 0) {
                        my_register.id = atoi(value_j);
                    } else if (strcmp(argument_j, "bitsize") == 0) {
                        my_register.bitsize = atoi(value_j);
                    } else if (strcmp(argument_j, "type") == 0) {
                        strcpy(my_register.type, value_j);
                    } else if (strcmp(argument_j, "group") == 0) {
                        strcpy(my_register.group, value_j);
                    }
                }
                /* add reg_type, internal_id and id*/
                my_register.reg_type = reg_type;
                my_register.internal_id = internal_id;
                internal_id++;
                if (!my_register.id) {
                    my_register.id = current_reg_id;
                    current_reg_id++;
                } else {
                    /* set correct ID for the next register */
                    current_reg_id = my_register.id + 1;
                }
                /* store register */
                g_array_append_vals(registers, (gconstpointer)&my_register, 1);
                /* free memory */
                g_array_free(reg_data, false);
            } else {
                /* store info for register */
                switch (c) {
                case ' ':
                    break;
                case '=':
                    is_argument = false;
                    break;
                case '"':
                    if (is_value) {
                        /* end of value reached */
                        is_value = false;
                        /* store arg-val combo */
                        xml_attrib current_attribute;
                        strcpy(current_attribute.argument, argument);
                        strcpy(current_attribute.value, value);
                        g_array_append_vals(reg_data,
                        (gconstpointer)&current_attribute, 1);
                        memset(argument, 0, sizeof(argument));
                        memset(value, 0, sizeof(value));
                    } else {
                        /*start of value */
                        is_value = true;
                    }
                    break;
                default:
                    if (is_argument) {
                        strncat(argument, c_ptr, 1);
                    } else if (is_value) {
                        strncat(value, c_ptr, 1);
                    } else {
                        is_argument = true;
                        strncat(argument, c_ptr, 1);
                    }
                    break;
                }
            }
        }
    }
}
