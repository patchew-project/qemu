/*
 * This is the main mcdstub.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/debug.h"
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
#include "cutils.h"

/* mcdstub header files */
#include "mcdstub/mcd_shared_defines.h"
#include "mcdstub/mcdstub.h"

/* architecture specific stubs */
#include "mcdstub/arm_mcdstub.h"

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

    /* create new debug object */
    mcd_init_debug_class();
 }

void mcd_set_stop_cpu(CPUState *cpu)
{
    mcdserver_state.c_cpu = cpu;
}

void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table)
{
    /* initalizes a list of all query commands */
    int cmd_number = 0;

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
    case TCP_CHAR_KILLQEMU:
        /* kill qemu completely */
        error_report("QEMU: Terminated via MCDstub");
        mcd_exit(0);
        exit(0);
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

int mcd_put_packet(const char *buf)
{
    return mcd_put_packet_binary(buf, strlen(buf));
}

void mcd_put_strbuf(void)
{
    mcd_put_packet(mcdserver_state.str_buf->str);
}

int mcd_put_packet_binary(const char *buf, int len)
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

CPUState *mcd_get_cpu(uint32_t cpu_index)
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
        mcd_vm_start();
    }
}

