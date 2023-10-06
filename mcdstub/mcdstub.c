/*
 * This is the main mcdstub.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "exec/mcdstub.h"
#include "mcdstub/syscalls.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "exec/replay-core.h"
#include "exec/hwaddr.h"

#include "qapi/error.h"
#include "exec/tb-flush.h"
#include "sysemu/cpus.h"
#include "sysemu/replay.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "monitor/monitor.h"

/* mcdstub header files */
#include "mcd_shared_defines.h"
#include "mcdstub.h"

/* architecture specific stubs */
#include "target/arm/mcdstub.h"

typedef struct {
    CharBackend chr;
} MCDSystemState;

MCDSystemState mcdserver_system_state;

MCDState mcdserver_state;

void mcd_init_mcdserver_state(void)
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
}

void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table)
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

    MCDCmdParseEntry query_state = {
        .handler = handle_query_state,
        .cmd = QUERY_ARG_STATE,
    };
    strcpy(query_state.schema, (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_state;
}

void reset_mcdserver_state(void)
{
    g_free(mcdserver_state.processes);
    mcdserver_state.processes = NULL;
    mcdserver_state.process_num = 0;
}

void create_processes(MCDState *s)
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

void mcd_create_default_process(MCDState *s)
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
    process->target_xml[0] = '\0';
}

int find_cpu_clusters(Object *child, void *opaque)
{
    if (object_dynamic_cast(child, TYPE_CPU_CLUSTER)) {
        MCDState *s = (MCDState *) opaque;
        CPUClusterState *cluster = CPU_CLUSTER(child);
        MCDProcess *process;

        s->processes = g_renew(MCDProcess, s->processes, ++s->process_num);

        process = &s->processes[s->process_num - 1];

        /*
         * GDB process IDs -1 and 0 are reserved. To avoid subtle errors at
         * runtime, we enforce here that the machine does not use a cluster ID
         * that would lead to PID 0.
         */
        assert(cluster->cluster_id != UINT32_MAX);
        process->pid = cluster->cluster_id + 1;
        process->attached = false;
        process->target_xml[0] = '\0';

        return 0;
    }

    return object_child_foreach(child, find_cpu_clusters, opaque);
}

int pid_order(const void *a, const void *b)
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

int mcdserver_start(const char *device)
{
    char mcdstub_device_name[128];
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

    /* if device == default -> set device = tcp::1235 */
    if (strcmp(device, "default") == 0) {
        device = "tcp::1235";
    }

    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(mcdstub_device_name, sizeof(mcdstub_device_name),
                     "%s,wait=off,nodelay=on,server=on", device);
            device = mcdstub_device_name;
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = mcd_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
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
    mcd_syscall_reset();

    return 0;
}

int mcd_chr_can_receive(void *opaque)
{
  return MAX_PACKET_LENGTH;
}

void mcd_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        mcd_read_byte(buf[i]);
        if (buf[i] == 0) {
            break;
        }
    }
}

void mcd_read_byte(uint8_t ch)
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

int mcd_handle_packet(const char *line_buf)
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
    case TCP_CHAR_GO:
        {
            static MCDCmdParseEntry go_cmd_desc = {
                .handler = handle_vm_start,
            };
            go_cmd_desc.cmd = (char[2]) { TCP_CHAR_GO, '\0' };
            strcpy(go_cmd_desc.schema,
                (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
            cmd_parser = &go_cmd_desc;
        }
        break;
    case TCP_CHAR_STEP:
        {
            static MCDCmdParseEntry step_cmd_desc = {
                .handler = handle_vm_step,
            };
            step_cmd_desc.cmd = (char[2]) { TCP_CHAR_STEP, '\0' };
            strcpy(step_cmd_desc.schema,
                (char[2]) { ARG_SCHEMA_CORENUM, '\0' });
            cmd_parser = &step_cmd_desc;
        }
        break;
    case TCP_CHAR_BREAK:
        {
            static MCDCmdParseEntry break_cmd_desc = {
                .handler = handle_vm_stop,
            };
            break_cmd_desc.cmd = (char[2]) { TCP_CHAR_BREAK, '\0' };
            cmd_parser = &break_cmd_desc;
        }
        break;
    case TCP_CHAR_KILLQEMU:
        /* kill qemu completely */
        error_report("QEMU: Terminated via MCDstub");
        mcd_exit(0);
        exit(0);
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
    case TCP_CHAR_RESET:
        {
            static MCDCmdParseEntry reset_cmd_desc = {
                .handler = handle_reset,
            };
            reset_cmd_desc.cmd = (char[2]) { TCP_CHAR_RESET, '\0' };
            strcpy(reset_cmd_desc.schema, (char[2]) { ARG_SCHEMA_INT, '\0' });
            cmd_parser = &reset_cmd_desc;
        }
        break;
    case TCP_CHAR_READ_REGISTER:
        {
            static MCDCmdParseEntry read_reg_cmd_desc = {
                .handler = handle_read_register,
            };
            read_reg_cmd_desc.cmd = (char[2]) { TCP_CHAR_READ_REGISTER, '\0' };
            strcpy(read_reg_cmd_desc.schema,
                (char[3]) { ARG_SCHEMA_CORENUM, ARG_SCHEMA_UINT64_T, '\0' });
            cmd_parser = &read_reg_cmd_desc;
        }
        break;
    case TCP_CHAR_WRITE_REGISTER:
        {
            static MCDCmdParseEntry write_reg_cmd_desc = {
                .handler = handle_write_register,
            };
            write_reg_cmd_desc.cmd =
                (char[2]) { TCP_CHAR_WRITE_REGISTER, '\0' };
            strcpy(write_reg_cmd_desc.schema,
                (char[5]) { ARG_SCHEMA_CORENUM, ARG_SCHEMA_UINT64_T,
                ARG_SCHEMA_INT, ARG_SCHEMA_HEXDATA, '\0' });
            cmd_parser = &write_reg_cmd_desc;
        }
        break;
    case TCP_CHAR_READ_MEMORY:
        {
            static MCDCmdParseEntry read_mem_cmd_desc = {
                .handler = handle_read_memory,
            };
            read_mem_cmd_desc.cmd = (char[2]) { TCP_CHAR_READ_MEMORY, '\0' };
            strcpy(read_mem_cmd_desc.schema,
                (char[5]) { ARG_SCHEMA_CORENUM, ARG_SCHEMA_INT,
                ARG_SCHEMA_UINT64_T, ARG_SCHEMA_INT, '\0' });
            cmd_parser = &read_mem_cmd_desc;
        }
        break;
    case TCP_CHAR_WRITE_MEMORY:
        {
            static MCDCmdParseEntry write_mem_cmd_desc = {
                .handler = handle_write_memory,
            };
            write_mem_cmd_desc.cmd = (char[2]) { TCP_CHAR_WRITE_MEMORY, '\0' };
            strcpy(write_mem_cmd_desc.schema,
                (char[6]) { ARG_SCHEMA_CORENUM, ARG_SCHEMA_INT,
                ARG_SCHEMA_UINT64_T, ARG_SCHEMA_INT,
                ARG_SCHEMA_HEXDATA, '\0' });
            cmd_parser = &write_mem_cmd_desc;
        }
        break;
    case TCP_CHAR_BREAKPOINT_INSERT:
        {
            static MCDCmdParseEntry handle_breakpoint_insert_cmd_desc = {
                .handler = handle_breakpoint_insert,
            };
            handle_breakpoint_insert_cmd_desc.cmd =
                (char[2]) { TCP_CHAR_BREAKPOINT_INSERT, '\0' };
            strcpy(handle_breakpoint_insert_cmd_desc.schema,
                (char[4]) { ARG_SCHEMA_CORENUM, ARG_SCHEMA_INT,
                ARG_SCHEMA_UINT64_T, '\0' });
            cmd_parser = &handle_breakpoint_insert_cmd_desc;
        }
        break;
    case TCP_CHAR_BREAKPOINT_REMOVE:
        {
            static MCDCmdParseEntry handle_breakpoint_remove_cmd_desc = {
                .handler = handle_breakpoint_remove,
            };
            handle_breakpoint_remove_cmd_desc.cmd =
                (char[2]) { TCP_CHAR_BREAKPOINT_REMOVE, '\0' };
            strcpy(handle_breakpoint_remove_cmd_desc.schema,
                (char[4]) { ARG_SCHEMA_CORENUM, ARG_SCHEMA_INT,
                ARG_SCHEMA_UINT64_T, '\0' });
            cmd_parser = &handle_breakpoint_remove_cmd_desc;
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

void handle_vm_start(GArray *params, void *user_ctx)
{
    /* TODO: add partial restart with arguments and so on */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    CPUState *cpu = mcd_get_cpu(cpu_id);
    mcd_cpu_start(cpu);
}

void handle_vm_step(GArray *params, void *user_ctx)
{
    /* TODO: add partial restart with arguments and so on */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;

    CPUState *cpu = mcd_get_cpu(cpu_id);
    int return_value = mcd_cpu_sstep(cpu);
    if (return_value != 0) {
        g_assert_not_reached();
    }
}


void handle_vm_stop(GArray *params, void *user_ctx)
{
    /* TODO: add partial stop with arguments and so on */
    mcd_vm_stop();
}

void handle_gen_query(GArray *params, void *user_ctx)
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

void run_cmd_parser(const char *data, const MCDCmdParseEntry *cmd)
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

uint64_t atouint64_t(const char *in)
{
    uint64_t res = 0;
    for (int i = 0; i < strlen(in); ++i) {
        const char c = in[i];
        res *= 10;
        res += c - '0';
    }

    return res;
}

uint32_t atouint32_t(const char *in)
{
    uint32_t res = 0;
    for (int i = 0; i < strlen(in); ++i) {
        const char c = in[i];
        res *= 10;
        res += c - '0';
    }

    return res;
}

int cmd_parse_params(const char *data, const char *schema, GArray *params)
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
            /* update remaining data for the next run */
            remaining_data = &(remaining_data[seperator_index + 1]);
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
            this_param.data_uint32_t = atouint32_t(data_buffer);
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_UINT64_T:
            this_param.data_uint64_t = atouint64_t(data_buffer);
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_QRYHANDLE:
            this_param.query_handle = atouint32_t(data_buffer);
            g_array_append_val(params, this_param);
            break;
        case ARG_SCHEMA_CORENUM:
            this_param.cpu_id = atouint32_t(data_buffer);
            g_array_append_val(params, this_param);
            break;
        default:
            return -1;
        }
    }
    return 0;
}

int process_string_cmd(void *user_ctx, const char *data,
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

void mcd_exit(int code)
{
    /* terminate qemu */
    if (!mcdserver_state.init) {
        return;
    }

    qemu_chr_fe_deinit(&mcdserver_system_state.chr, true);
}

void mcd_chr_event(void *opaque, QEMUChrEvent event)
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

bool mcd_supports_guest_debug(void)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->supports_guest_debug) {
        return ops->supports_guest_debug();
    }
    return false;
}

#ifndef _WIN32
void mcd_sigterm_handler(int signal)
{
    if (runstate_is_running()) {
        vm_stop(RUN_STATE_PAUSED);
    }
}
#endif

void mcd_vm_state_change(void *opaque, bool running, RunState state)
{
    CPUState *cpu = mcdserver_state.c_cpu;

    if (mcdserver_state.state == RS_INACTIVE) {
        return;
    }

    if (cpu == NULL) {
        if (running) {
            /*
             * this is the case if qemu starts the vm
             * before a mcd client is connected
             */
            const char *mcd_state;
            mcd_state = CORE_STATE_RUNNING;
            const char *info_str;
            info_str = STATE_STR_INIT_RUNNING;
            mcdserver_state.cpu_state.state = mcd_state;
            mcdserver_state.cpu_state.info_str = info_str;
        }
        return;
    }

    const char *mcd_state;
    const char *stop_str;
    const char *info_str;
    uint32_t bp_type = 0;
    uint64_t bp_address = 0;
    switch (state) {
    case RUN_STATE_RUNNING:
        mcd_state = CORE_STATE_RUNNING;
        info_str = STATE_STR_RUNNING(cpu->cpu_index);
        stop_str = "";
        break;
    case RUN_STATE_DEBUG:
        mcd_state = CORE_STATE_DEBUG;
        info_str = STATE_STR_DEBUG(cpu->cpu_index);
        if (cpu->watchpoint_hit) {
            switch (cpu->watchpoint_hit->flags & BP_MEM_ACCESS) {
            case BP_MEM_READ:
                bp_type = MCD_BREAKPOINT_READ;
                stop_str = STATE_STR_BREAK_READ(cpu->watchpoint_hit->hitaddr);
                break;
            case BP_MEM_WRITE:
                bp_type = MCD_BREAKPOINT_WRITE;
                stop_str = STATE_STR_BREAK_WRITE(cpu->watchpoint_hit->hitaddr);
                break;
            case BP_MEM_ACCESS:
                bp_type = MCD_BREAKPOINT_RW;
                stop_str = STATE_STR_BREAK_RW(cpu->watchpoint_hit->hitaddr);
                break;
            default:
                stop_str = STATE_STR_BREAK_UNKNOWN;
                break;
            }
            bp_address = cpu->watchpoint_hit->hitaddr;
            cpu->watchpoint_hit = NULL;
        } else if (cpu->singlestep_enabled) {
            /* we land here when a single step is performed */
            stop_str = STATE_STEP_PERFORMED;
        } else {
            bp_type = MCD_BREAKPOINT_HW;
            stop_str = STATE_STR_BREAK_HW;
            tb_flush(cpu);
        }
        /* deactivate single step */
        cpu_single_step(cpu, 0);
        break;
    case RUN_STATE_PAUSED:
        info_str = STATE_STR_HALTED(cpu->cpu_index);
        mcd_state = CORE_STATE_HALTED;
        stop_str = "";
        break;
    case RUN_STATE_WATCHDOG:
        info_str = STATE_STR_UNKNOWN(cpu->cpu_index);
        mcd_state = CORE_STATE_UNKNOWN;
        stop_str = "";
        break;
    default:
        info_str = STATE_STR_UNKNOWN(cpu->cpu_index);
        mcd_state = CORE_STATE_UNKNOWN;
        stop_str = "";
        break;
    }

    /* set state for c_cpu */
    mcdserver_state.cpu_state.state = mcd_state;
    mcdserver_state.cpu_state.bp_type = bp_type;
    mcdserver_state.cpu_state.bp_address = bp_address;
    mcdserver_state.cpu_state.stop_str = stop_str;
    mcdserver_state.cpu_state.info_str = info_str;
}

int mcd_put_packet(const char *buf)
{
    return mcd_put_packet_binary(buf, strlen(buf), false);
}

void mcd_put_strbuf(void)
{
    mcd_put_packet(mcdserver_state.str_buf->str);
}

int mcd_put_packet_binary(const char *buf, int len, bool dump)
{
    for (;;) {
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

        if (mcd_got_immediate_ack()) {
            break;
        }
    }
    return 0;
}

bool mcd_got_immediate_ack(void)
{
    return true;
}

void mcd_put_buffer(const uint8_t *buf, int len)
{
    qemu_chr_fe_write_all(&mcdserver_system_state.chr, buf, len);
}

MCDProcess *mcd_get_cpu_process(CPUState *cpu)
{
    return mcd_get_process(mcd_get_cpu_pid(cpu));
}

uint32_t mcd_get_cpu_pid(CPUState *cpu)
{
    if (cpu->cluster_index == UNASSIGNED_CLUSTER_INDEX) {
        /* Return the default process' PID */
        int index = mcdserver_state.process_num - 1;
        return mcdserver_state.processes[index].pid;
    }
    return cpu->cluster_index + 1;
}

MCDProcess *mcd_get_process(uint32_t pid)
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

CPUState *mcd_get_cpu(uint32_t i_cpu_index)
{
    CPUState *cpu = first_cpu;

    while (cpu) {
        if (cpu->cpu_index == i_cpu_index) {
            return cpu;
        }
        cpu = mcd_next_attached_cpu(cpu);
    }

    return cpu;
}

CPUState *mcd_first_attached_cpu(void)
{
    CPUState *cpu = first_cpu;
    MCDProcess *process = mcd_get_cpu_process(cpu);

    if (!process->attached) {
        return mcd_next_attached_cpu(cpu);
    }

    return cpu;
}

CPUState *mcd_next_attached_cpu(CPUState *cpu)
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

int mcd_get_cpu_index(CPUState *cpu)
{
    return cpu->cpu_index + 1;
}

CPUState *get_first_cpu_in_process(MCDProcess *process)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (mcd_get_cpu_pid(cpu) == process->pid) {
            return cpu;
        }
    }

    return NULL;
}

CPUState *find_cpu(uint32_t thread_id)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (mcd_get_cpu_index(cpu) == thread_id) {
            return cpu;
        }
    }

    return NULL;
}


void parse_reg_xml(const char *xml, int size, GArray *registers,
    uint8_t reg_type)
{
    /* iterates over the complete xml file */
    int i, j;
    uint32_t internal_id = 0;
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
                    /*
                     * we might want to read out the regnum
                     * } else if (strcmp(argument_j, "regnum") == 0) {
                     * my_register.internal_id = atoi(value_j);
                     */
                    } else if (strcmp(argument_j, "bitsize") == 0) {
                        my_register.bitsize = atoi(value_j);
                    } else if (strcmp(argument_j, "type") == 0) {
                        strcpy(my_register.type, value_j);
                    } else if (strcmp(argument_j, "group") == 0) {
                        strcpy(my_register.group, value_j);
                    }
                }
                /* add reg_type and internal id */
                my_register.reg_type = reg_type;
                my_register.internal_id = internal_id;
                internal_id++;
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

int int_cmp(gconstpointer a, gconstpointer b)
{
    int int_a = *(int *)a;
    int int_b = *(int *)b;
    if (int_a == int_b) {
        return 0;
    } else {
        return 1;
    }
}

int mcd_arm_store_mem_spaces(CPUState *cpu, GArray *memspaces)
{
    int nr_address_spaces = cpu->num_ases;
    uint32_t mem_space_id = 0;

    /*
     * TODO: atm we can only access physical memory addresses,
     * but trace32 needs fake locical spaces to work with
    */

    mem_space_id++;
    mcd_mem_space_st non_secure = {
        .name = "Non Secure",
        .id = mem_space_id,
        .type = 34,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
        .is_secure = false,
    };
    g_array_append_vals(memspaces, (gconstpointer)&non_secure, 1);
    mem_space_id++;
    mcd_mem_space_st phys_non_secure = {
        .name = "Physical (Non Secure)",
        .id = mem_space_id,
        .type = 18,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
        .is_secure = false,
    };
    g_array_append_vals(memspaces, (gconstpointer)&phys_non_secure, 1);
    if(nr_address_spaces > 1) {
        mem_space_id++;
        mcd_mem_space_st secure = {
            .name = "Secure",
            .id = mem_space_id,
            .type = 34,
            .bits_per_mau = 8,
            .invariance = 1,
            .endian = 1,
            .min_addr = 0,
            .max_addr = -1,
            .supported_access_options = 0,
            .is_secure = true,
        };
        g_array_append_vals(memspaces, (gconstpointer)&secure, 1);
        mem_space_id++;
        mcd_mem_space_st phys_secure = {
            .name = "Physical (Secure)",
            .id = mem_space_id,
            .type = 18,
            .bits_per_mau = 8,
            .invariance = 1,
            .endian = 1,
            .min_addr = 0,
            .max_addr = -1,
            .supported_access_options = 0,
            .is_secure = true,
        };
        g_array_append_vals(memspaces, (gconstpointer)&phys_secure, 1);
    }
    /* TODO: get dynamically how the per (CP15) space is called */
    mem_space_id++;
    mcd_mem_space_st gpr = {
        .name = "GPR Registers",
        .id = mem_space_id,
        .type = 1,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&gpr, 1);
    mem_space_id++;
    mcd_mem_space_st cpr = {
        .name = "CP15 Registers",
        .id = mem_space_id,
        .type = 1,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&cpr, 1);
    return 0;
}

int init_resets(GArray *resets)
{
    mcd_reset_st system_reset = { .id = 0, .name = RESET_SYSTEM};
    mcd_reset_st gpr_reset = { .id = 1, .name = RESET_GPR};
    mcd_reset_st memory_reset = { .id = 2, .name = RESET_MEMORY};
    g_array_append_vals(resets, (gconstpointer)&system_reset, 1);
    g_array_append_vals(resets, (gconstpointer)&gpr_reset, 1);
    g_array_append_vals(resets, (gconstpointer)&memory_reset, 1);
    return 0;
}

int init_trigger(mcd_trigger_into_st *trigger)
{
    trigger->type = (MCD_TRIG_TYPE_IP | MCD_TRIG_TYPE_READ |
        MCD_TRIG_TYPE_WRITE | MCD_TRIG_TYPE_RW);
    trigger->option = (MCD_TRIG_OPT_DATA_IS_CONDITION);
    trigger->action = (MCD_TRIG_ACTION_DBG_DEBUG);
    /* there is no specific upper limit for trigger */
    trigger->nr_trigger = 0;
    return 0;
}

void handle_open_server(GArray *params, void *user_ctx)
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

void handle_query_system(GArray *params, void *user_ctx)
{
    mcd_put_packet(MCD_SYSTEM_NAME);
}

void handle_query_cores(GArray *params, void *user_ctx)
{
    /* get first cpu */
    CPUState *cpu = mcd_first_attached_cpu();
    if (!cpu) {
        return;
    }

    ObjectClass *oc = object_get_class(OBJECT(cpu));
    const char *cpu_model = object_class_get_name(oc);

    CPUClass *cc = CPU_GET_CLASS(cpu);
    gchar *arch = cc->gdb_arch_name(cpu);

    int nr_cores = cpu->nr_cores;
    char device_name[] = DEVICE_NAME_TEMPLATE(arch);
    g_string_printf(mcdserver_state.str_buf, "%s=%s.%s=%s.%s=%d.",
        TCP_ARGUMENT_DEVICE, device_name, TCP_ARGUMENT_CORE, cpu_model,
        TCP_ARGUMENT_AMOUNT_CORE, nr_cores);
    mcd_put_strbuf();
    g_free(arch);
}

int mcd_arm_parse_core_xml_file(CPUClass *cc, GArray *reggroups,
    GArray *registers, int *current_group_id)
{
    const char *xml_filename = NULL;
    const char *current_xml_filename = NULL;
    const char *xml_content = NULL;
    int i = 0;

    /* 1. get correct file */
    xml_filename = cc->gdb_core_xml_file;
    for (i = 0; ; i++) {
        current_xml_filename = xml_builtin[i][0];
        if (!current_xml_filename || (strncmp(current_xml_filename,
            xml_filename, strlen(xml_filename)) == 0
            && strlen(current_xml_filename) == strlen(xml_filename)))
            break;
    }
    /* without gpr registers we can do nothing */
    if (!current_xml_filename) {
        return -1;
    }

    /* 2. add group for gpr registers */
    mcd_reg_group_st gprregs = {
        .name = "GPR Registers",
        .id = *current_group_id
    };
    g_array_append_vals(reggroups, (gconstpointer)&gprregs, 1);
    *current_group_id = *current_group_id + 1;

    /* 3. parse xml */
    xml_content = xml_builtin[i][1];
    parse_reg_xml(xml_content, strlen(xml_content), registers,
        MCD_ARM_REG_TYPE_GPR);
    return 0;
}

int mcd_arm_parse_general_xml_files(CPUState *cpu, GArray *reggroups,
    GArray *registers, int *current_group_id) {
    const char *xml_filename = NULL;
    const char *current_xml_filename = NULL;
    const char *xml_content = NULL;
    int i = 0;
    uint8_t reg_type;

    /* iterate over all gdb xml files*/
    GDBRegisterState *r;
    for (r = cpu->gdb_regs; r; r = r->next) {
        xml_filename = r->xml;
        xml_content = NULL;

        /* 1. get xml content */
        xml_content = arm_mcd_get_dynamic_xml(cpu, xml_filename);
        if (xml_content) {
            if (strcmp(xml_filename, "system-registers.xml") == 0) {
                /* these are the coprocessor register */
                mcd_reg_group_st corprocessorregs = {
                    .name = "CP15 Registers",
                    .id = *current_group_id
                };
                g_array_append_vals(reggroups,
                    (gconstpointer)&corprocessorregs, 1);
                *current_group_id = *current_group_id + 1;
                reg_type = MCD_ARM_REG_TYPE_CPR;
            }
        } else {
            /* its not a coprocessor xml -> it is a static xml file */
            for (i = 0; ; i++) {
                current_xml_filename = xml_builtin[i][0];
                if (!current_xml_filename || (strncmp(current_xml_filename,
                    xml_filename, strlen(xml_filename)) == 0
                    && strlen(current_xml_filename) == strlen(xml_filename)))
                    break;
            }
            if (current_xml_filename) {
                xml_content = xml_builtin[i][1];
                /* select correct reg_type */
                if (strcmp(current_xml_filename, "arm-vfp.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP;
                } else if (strcmp(current_xml_filename, "arm-vfp3.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP;
                } else if (strcmp(current_xml_filename,
                    "arm-vfp-sysregs.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP_SYS;
                } else if (strcmp(current_xml_filename,
                    "arm-neon.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_VFP;
                } else if (strcmp(current_xml_filename,
                    "arm-m-profile-mve.xml") == 0) {
                    reg_type = MCD_ARM_REG_TYPE_MVE;
                }
            } else {
                continue;
            }
        }
        /* 2. parse xml */
        parse_reg_xml(xml_content, strlen(xml_content), registers, reg_type);
    }
    return 0;
}

int mcd_arm_get_additional_register_info(GArray *reggroups, GArray *registers,
    CPUState *cpu)
{
    mcd_reg_st *current_register;
    uint32_t i = 0;

    /* iterate over all registers */
    for (i = 0; i < registers->len; i++) {
        current_register = &(g_array_index(registers, mcd_reg_st, i));
        current_register->id = i;
        /* add mcd_reg_group_id and mcd_mem_space_id */
        if (strcmp(current_register->group, "cp_regs") == 0) {
            /* coprocessor registers */
            current_register->mcd_reg_group_id = 2;
            current_register->mcd_mem_space_id = 6;
            /*
             * get info for opcode
             * for 32bit the opcode is only 16 bit long
             * for 64bit it is 32 bit long
             */
            current_register->opcode |=
                arm_mcd_get_opcode(cpu, current_register->internal_id);
        } else {
            /* gpr register */
            current_register->mcd_reg_group_id = 1;
            current_register->mcd_mem_space_id = 5;
        }
    }
    return 0;
}

void handle_open_core(GArray *params, void *user_ctx)
{
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    CPUState *cpu = mcd_get_cpu(cpu_id);
    mcdserver_state.c_cpu = cpu;
    CPUClass *cc = CPU_GET_CLASS(cpu);
    gchar *arch = cc->gdb_arch_name(cpu);
    int return_value = 0;

    /* prepare data strucutures */
    GArray *memspaces = g_array_new(false, true, sizeof(mcd_mem_space_st));
    GArray *reggroups = g_array_new(false, true, sizeof(mcd_reg_group_st));
    GArray *registers = g_array_new(false, true, sizeof(mcd_reg_st));

    if (strcmp(arch, "arm") == 0) {
        /* TODO: make group and memspace ids dynamic */
        int current_group_id = 1;
        /* 1. store mem spaces */
        return_value = mcd_arm_store_mem_spaces(cpu, memspaces);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 2. parse core xml */
        return_value = mcd_arm_parse_core_xml_file(cc, reggroups,
            registers, &current_group_id);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 3. parse other xmls */
        return_value = mcd_arm_parse_general_xml_files(cpu, reggroups,
            registers, &current_group_id);
        if (return_value != 0) {
            g_assert_not_reached();
        }
        /* 4. add additional data the the regs from the xmls */
        return_value = mcd_arm_get_additional_register_info(reggroups,
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

    g_free(arch);
}

void handle_query_reset_f(GArray *params, void *user_ctx)
{
    /* TODO: vull reset over the qemu monitor */

    /* 1. check length */
    int nb_resets = mcdserver_state.resets->len;
    if (nb_resets == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    /* 2. send data */
    mcd_reset_st reset = g_array_index(mcdserver_state.resets, mcd_reset_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%d.",
        TCP_ARGUMENT_NAME, reset.name, TCP_ARGUMENT_ID, reset.id);
    mcd_put_strbuf();
}

void handle_query_reset_c(GArray *params, void *user_ctx)
{
    /* reset options are the same for every cpu! */
    uint32_t query_index = get_param(params, 0)->query_handle;

    /* 1. check weather this was the last mem space */
    int nb_groups = mcdserver_state.resets->len;
    if (query_index + 1 == nb_groups) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index + 1);
    }

    /* 2. send data */
    mcd_reset_st reset = g_array_index(mcdserver_state.resets,
        mcd_reset_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%d.",
        TCP_ARGUMENT_NAME, reset.name, TCP_ARGUMENT_ID, reset.id);
    mcd_put_strbuf();
}

void handle_close_core(GArray *params, void *user_ctx)
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

void handle_close_server(GArray *params, void *user_ctx)
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
        mcd_disable_syscalls();
        mcd_vm_start();
    }
}

void handle_query_trigger(GArray *params, void *user_ctx)
{
    mcd_trigger_into_st trigger = mcdserver_state.trigger;
    g_string_printf(mcdserver_state.str_buf, "%s=%d.%s=%d.%s=%d.%s=%d.",
        TCP_ARGUMENT_AMOUNT_TRIGGER, trigger.nr_trigger,
        TCP_ARGUMENT_TYPE, trigger.type, TCP_ARGUMENT_OPTION, trigger.option,
        TCP_ARGUMENT_ACTION, trigger.action);
    mcd_put_strbuf();
}

void mcd_vm_start(void)
{
    if (!runstate_needs_reset() && !runstate_is_running()) {
        vm_start();
    }
}

void mcd_cpu_start(CPUState *cpu)
{
    if (!runstate_needs_reset() && !runstate_is_running() &&
        !vm_prepare_start(false)) {
        mcdserver_state.c_cpu = cpu;
        qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
        cpu_resume(cpu);
    }
}

int mcd_cpu_sstep(CPUState *cpu)
{
    mcdserver_state.c_cpu = cpu;
    cpu_single_step(cpu, mcdserver_state.sstep_flags);
    if (!runstate_needs_reset() && !runstate_is_running() &&
        !vm_prepare_start(true)) {
        qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
        cpu_resume(cpu);
    }
    return 0;
}

void mcd_vm_stop(void)
{
    if (runstate_is_running()) {
        vm_stop(RUN_STATE_DEBUG);
    }
}

void handle_query_mem_spaces_f(GArray *params, void *user_ctx)
{
    /* 1. get correct memspaces and set the query_cpu */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray *memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);

    /* 2. check length */
    int nb_groups = memspaces->len;
    if (nb_groups == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }

    /* 3. send data */
    mcd_mem_space_st space = g_array_index(memspaces, mcd_mem_space_st, 0);
    g_string_append_printf(mcdserver_state.str_buf,
        "%s=%s.%s=%d.%s=%d.%s=%d.%s=%d.%s=%d.%s=%ld.%s=%ld.%s=%d.",
        TCP_ARGUMENT_NAME, space.name, TCP_ARGUMENT_ID, space.id,
        TCP_ARGUMENT_TYPE, space.type, TCP_ARGUMENT_BITS_PER_MAU,
        space.bits_per_mau, TCP_ARGUMENT_INVARIANCE, space.invariance,
        TCP_ARGUMENT_ENDIAN, space.endian, TCP_ARGUMENT_MIN, space.min_addr,
        TCP_ARGUMENT_MAX, space.max_addr, TCP_ARGUMENT_SUPPORTED_ACCESS_OPTIONS,
        space.supported_access_options);
    mcd_put_strbuf();
}

void handle_query_mem_spaces_c(GArray *params, void *user_ctx)
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
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index + 1);
    }

    /* 3. send the correct memspace */
    mcd_mem_space_st space = g_array_index(memspaces,
        mcd_mem_space_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf,
        "%s=%s.%s=%d.%s=%d.%s=%d.%s=%d.%s=%d.%s=%ld.%s=%ld.%s=%d.",
        TCP_ARGUMENT_NAME, space.name, TCP_ARGUMENT_ID,
        space.id, TCP_ARGUMENT_TYPE, space.type, TCP_ARGUMENT_BITS_PER_MAU,
        space.bits_per_mau, TCP_ARGUMENT_INVARIANCE, space.invariance,
        TCP_ARGUMENT_ENDIAN, space.endian, TCP_ARGUMENT_MIN, space.min_addr,
        TCP_ARGUMENT_MAX, space.max_addr, TCP_ARGUMENT_SUPPORTED_ACCESS_OPTIONS,
        space.supported_access_options);
    mcd_put_strbuf();
}

void handle_query_reg_groups_f(GArray *params, void *user_ctx)
{
    /* 1. get correct reggroups and set the query_cpu */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray *reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);

    /* 2. check length */
    int nb_groups = reggroups->len;
    if (nb_groups == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    /* 3. send data */
    mcd_reg_group_st group = g_array_index(reggroups, mcd_reg_group_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%d.%s=%s.",
        TCP_ARGUMENT_ID, group.id, TCP_ARGUMENT_NAME, group.name);
    mcd_put_strbuf();
}

void handle_query_reg_groups_c(GArray *params, void *user_ctx)
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
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index + 1);
    }

    /* 3. send the correct reggroup */
    mcd_reg_group_st group = g_array_index(reggroups, mcd_reg_group_st,
        query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%d.%s=%s.",
        TCP_ARGUMENT_ID, group.id, TCP_ARGUMENT_NAME, group.name);
    mcd_put_strbuf();
}

void handle_query_regs_f(GArray *params, void *user_ctx)
{
    /* 1. get correct registers and set the query_cpu */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray *registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);

    /* 2. check length */
    int nb_regs = registers->len;
    if (nb_regs == 1) {
        /* indicates this is the last packet */
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "1!");
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

void handle_query_regs_c(GArray *params, void *user_ctx)
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
        g_string_printf(mcdserver_state.str_buf, "0!");
    } else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index + 1);
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

void handle_reset(GArray *params, void *user_ctx)
{
    /*
     * int reset_id = get_param(params, 0)->data_int;
     * TODO: implement resets
     */
}

void handle_query_state(GArray *params, void *user_ctx)
{
    /*
     * TODO: multicore support
     * get state info
     */
    mcd_cpu_state_st state_info = mcdserver_state.cpu_state;
    mcd_core_event_et event = MCD_CORE_EVENT_NONE;
    if (state_info.memory_changed) {
        event = event | MCD_CORE_EVENT_MEMORY_CHANGE;
        state_info.memory_changed = false;
    }
    if (state_info.registers_changed) {
        event = event | MCD_CORE_EVENT_REGISTER_CHANGE;
        state_info.registers_changed = false;
    }
    if (state_info.target_was_stopped) {
        event = event | MCD_CORE_EVENT_STOPPED;
        state_info.target_was_stopped = false;
    }
    /* send data */
    g_string_printf(mcdserver_state.str_buf,
        "%s=%s.%s=%u.%s=%u.%s=%u.%s=%lu.%s=%s.%s=%s.",
        TCP_ARGUMENT_STATE, state_info.state,
        TCP_ARGUMENT_EVENT, event, TCP_ARGUMENT_THREAD, 0,
        TCP_ARGUMENT_TYPE, state_info.bp_type,
        TCP_ARGUMENT_ADDRESS, state_info.bp_address,
        TCP_ARGUMENT_STOP_STRING, state_info.stop_str,
        TCP_ARGUMENT_INFO_STRING, state_info.info_str);
    mcd_put_strbuf();

    /* reset debug info after first query */
    if (strcmp(state_info.state, CORE_STATE_DEBUG) == 0) {
        mcdserver_state.cpu_state.stop_str = "";
        mcdserver_state.cpu_state.info_str = "";
        mcdserver_state.cpu_state.bp_type = 0;
        mcdserver_state.cpu_state.bp_address = 0;
    }
}

int mcd_read_register(CPUState *cpu, GByteArray *buf, int reg)
{
    /* 1. get reg type and internal id */
    GArray *registers =
        g_list_nth_data(mcdserver_state.all_registers, cpu->cpu_index);
    mcd_reg_st desired_register = g_array_index(registers, mcd_reg_st, reg);
    uint8_t reg_type = desired_register.reg_type;
    uint32_t internal_id = desired_register.internal_id;
    /* 2. read register */
    CPUClass *cc = CPU_GET_CLASS(cpu);
    gchar *arch = cc->gdb_arch_name(cpu);
    if (strcmp(arch, "arm") == 0) {
        g_free(arch);
        return arm_mcd_read_register(cpu, buf, reg_type, internal_id);
    } else {
        g_free(arch);
        return 0;
    }
}

int mcd_write_register(CPUState *cpu, GByteArray *buf, int reg)
{
    /* 1. get reg type and internal id */
    GArray *registers =
        g_list_nth_data(mcdserver_state.all_registers, cpu->cpu_index);
    mcd_reg_st desired_register = g_array_index(registers, mcd_reg_st, reg);
    uint8_t reg_type = desired_register.reg_type;
    uint32_t internal_id = desired_register.internal_id;
    /* 2. write register */
    CPUClass *cc = CPU_GET_CLASS(cpu);
    gchar *arch = cc->gdb_arch_name(cpu);
    if (strcmp(arch, "arm") == 0) {
        g_free(arch);
        return arm_mcd_write_register(cpu, buf, reg_type, internal_id);
    } else {
        g_free(arch);
        return 0;
    }
}

void mcd_memtohex(GString *buf, const uint8_t *mem, int len)
{
    int i, c;
    for (i = 0; i < len; i++) {
        c = mem[i];
        g_string_append_c(buf, tohex(c >> 4));
        g_string_append_c(buf, tohex(c & 0xf));
    }
    g_string_append_c(buf, '\0');
}

void mcd_hextomem(GByteArray *mem, const char *buf, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        guint8 byte = fromhex(buf[0]) << 4 | fromhex(buf[1]);
        g_byte_array_append(mem, &byte, 1);
        buf += 2;
    }
}

void handle_read_register(GArray *params, void *user_ctx)
{
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    uint64_t reg_num = get_param(params, 1)->data_uint64_t;
    int reg_size;

    CPUState *cpu = mcd_get_cpu(cpu_id);
    reg_size = mcd_read_register(cpu, mcdserver_state.mem_buf, reg_num);
    mcd_memtohex(mcdserver_state.str_buf,
        mcdserver_state.mem_buf->data, reg_size);
    mcd_put_strbuf();
}

void handle_write_register(GArray *params, void *user_ctx)
{
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    uint64_t reg_num = get_param(params, 1)->data_uint64_t;
    uint32_t reg_size = get_param(params, 2)->data_uint32_t;

    CPUState *cpu = mcd_get_cpu(cpu_id);
    mcd_hextomem(mcdserver_state.mem_buf,
        mcdserver_state.str_buf->str, reg_size);
    if (mcd_write_register(cpu, mcdserver_state.mem_buf, reg_num) == 0) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
    } else {
        mcd_put_packet(TCP_EXECUTION_SUCCESS);
    }
}

int mcd_read_memory(CPUState *cpu, hwaddr addr, uint8_t *buf, int len)
{
    CPUClass *cc;
    /*TODO: add physical mem cpu_physical_memory_read(addr, buf, len); */
    cc = CPU_GET_CLASS(cpu);
    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, false);
    }

    return cpu_memory_rw_debug(cpu, addr, buf, len, false);
}

int mcd_write_memory(CPUState *cpu, hwaddr addr, uint8_t *buf, int len)
{
    CPUClass *cc;
    /*TODO: add physical mem cpu_physical_memory_read(addr, buf, len); */
    cc = CPU_GET_CLASS(cpu);
    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, true);
    }

    return cpu_memory_rw_debug(cpu, addr, buf, len, true);
}

void handle_read_memory(GArray *params, void *user_ctx)
{
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    uint32_t mem_space_id = get_param(params, 1)->data_uint32_t;
    uint64_t mem_address = get_param(params, 2)->data_uint64_t;
    uint32_t len = get_param(params, 3)->data_uint32_t;

    CPUState *cpu = mcd_get_cpu(cpu_id);
    /* check if the mem space is secure */
    GArray *memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);
    mcd_mem_space_st space = g_array_index(memspaces, mcd_mem_space_st,
        mem_space_id - 1);
    if (arm_mcd_set_scr(cpu, space.is_secure)) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
        return;
    }
    /* read memory */
    g_byte_array_set_size(mcdserver_state.mem_buf, len);
    if (mcd_read_memory(cpu, mem_address, mcdserver_state.mem_buf->data,
        mcdserver_state.mem_buf->len) != 0) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
    } else {
        mcd_memtohex(mcdserver_state.str_buf, mcdserver_state.mem_buf->data,
            mcdserver_state.mem_buf->len);
        mcd_put_strbuf();
    }
}

void handle_write_memory(GArray *params, void *user_ctx)
{
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    uint32_t mem_space_id = get_param(params, 1)->data_uint32_t;
    uint64_t mem_address = get_param(params, 2)->data_uint64_t;
    uint32_t len = get_param(params, 3)->data_uint32_t;
    CPUState *cpu = mcd_get_cpu(cpu_id);
    /* check if the mem space is secure */
    GArray *memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);
    mcd_mem_space_st space = g_array_index(memspaces, mcd_mem_space_st,
        mem_space_id - 1);
    if (arm_mcd_set_scr(cpu, space.is_secure)) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
        return;
    }
    /* read memory */
    mcd_hextomem(mcdserver_state.mem_buf, mcdserver_state.str_buf->str, len);
    if (mcd_write_memory(cpu, mem_address,
        mcdserver_state.mem_buf->data, len) != 0) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
    } else {
        mcd_put_packet(TCP_EXECUTION_SUCCESS);
    }
}

int mcd_breakpoint_insert(CPUState *cpu, int type, vaddr addr)
{
    /* translate the type to known gdb types and function call*/
    int bp_type = 0;
    CPUClass *cc = CPU_GET_CLASS(cpu);
    if (cc->gdb_stop_before_watchpoint) {
        //bp_type |= BP_STOP_BEFORE_ACCESS;
    }
    int return_value = 0;
    switch (type) {
    case MCD_BREAKPOINT_HW:
        return_value = cpu_breakpoint_insert(cpu, addr, BP_GDB, NULL);
        return return_value;
    case MCD_BREAKPOINT_READ:
        bp_type |= BP_GDB | BP_MEM_READ;
        return_value = cpu_watchpoint_insert(cpu, addr, 4, bp_type, NULL);
        return return_value;
    case MCD_BREAKPOINT_WRITE:
        bp_type |= BP_GDB | BP_MEM_WRITE;
        return_value = cpu_watchpoint_insert(cpu, addr, 4, bp_type, NULL);
        return return_value;
    case MCD_BREAKPOINT_RW:
        bp_type |= BP_GDB | BP_MEM_ACCESS;
        return_value = cpu_watchpoint_insert(cpu, addr, 4, bp_type, NULL);
        return return_value;
    default:
        return -ENOSYS;
    }
}

int mcd_breakpoint_remove(CPUState *cpu, int type, vaddr addr)
{
    /* translate the type to known gdb types and function call*/
    int bp_type = 0;
    CPUClass *cc = CPU_GET_CLASS(cpu);
    if (cc->gdb_stop_before_watchpoint) {
        //bp_type |= BP_STOP_BEFORE_ACCESS;
    }
    int return_value = 0;
    switch (type) {
    case MCD_BREAKPOINT_HW:
        return_value = cpu_breakpoint_remove(cpu, addr, BP_GDB);
        return return_value;
    case MCD_BREAKPOINT_READ:
        bp_type |= BP_GDB | BP_MEM_READ;
        return_value = cpu_watchpoint_remove(cpu, addr, 4, bp_type);
        return return_value;
    case MCD_BREAKPOINT_WRITE:
        bp_type |= BP_GDB | BP_MEM_WRITE;
        return_value = cpu_watchpoint_remove(cpu, addr, 4, bp_type);
        return return_value;
    case MCD_BREAKPOINT_RW:
        bp_type |= BP_GDB | BP_MEM_ACCESS;
        return_value = cpu_watchpoint_remove(cpu, addr, 4, bp_type);
        return return_value;
    default:
        return -ENOSYS;
    }
}

void handle_breakpoint_insert(GArray *params, void *user_ctx)
{
    /* 1. get parameter data */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    uint32_t type = get_param(params, 1)->data_uint32_t;
    uint64_t address = get_param(params, 2)->data_uint64_t;
    /* 2. insert breakpoint and send reply */
    CPUState *cpu = mcd_get_cpu(cpu_id);
    if (mcd_breakpoint_insert(cpu, type, address) != 0) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
    } else {
        mcd_put_packet(TCP_EXECUTION_SUCCESS);
    }
}

void handle_breakpoint_remove(GArray *params, void *user_ctx)
{
    /* 1. get parameter data */
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    uint32_t type = get_param(params, 1)->data_uint32_t;
    uint64_t address = get_param(params, 2)->data_uint64_t;
    /* 2. remove breakpoint and send reply */
    CPUState *cpu = mcd_get_cpu(cpu_id);
    if (mcd_breakpoint_remove(cpu, type, address) != 0) {
        mcd_put_packet(TCP_EXECUTION_ERROR);
    } else {
        mcd_put_packet(TCP_EXECUTION_SUCCESS);
    }
}
