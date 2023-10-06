/*
 * This is the main mcdstub. It needs to be complemented by other mcd stubs for each target.
 */

#include "mcd_shared_defines.h"

//from original gdbstub.c
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

#include "internals.h"

//from original softmmu.c (minus what was already here)
#include "qapi/error.h"
#include "exec/tb-flush.h"
#include "sysemu/cpus.h"
#include "sysemu/replay.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "monitor/monitor.h"

//architecture specific stuff
#include "target/arm/mcdstub.h"

// FIXME: delete the following line and check if it worked
#include "hw/core/sysemu-cpu-ops.h"

typedef struct {
    CharBackend chr;
    //Chardev *mon_chr;
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
	// TODO:
	// this is weird and we might not actually need it after all
    mcdserver_state.supported_sstep_flags = accel_supported_gdbstub_sstep_flags();
    mcdserver_state.sstep_flags = SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
    mcdserver_state.sstep_flags &= mcdserver_state.supported_sstep_flags;

    // init query table
    init_query_cmds_table(mcdserver_state.mcd_query_cmds_table);
}

void init_query_cmds_table(MCDCmdParseEntry* mcd_query_cmds_table) {
    // initalizes a list of all query commands
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
    strcpy(query_reset_c.schema, (char[2]) { (char) ARG_SCHEMA_QRYHANDLE, '\0' });
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
    strcpy(query_mem_spaces_f.schema, (char[2]) { (char) ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_mem_spaces_f;
    cmd_number++;

    MCDCmdParseEntry query_mem_spaces_c = {
        .handler = handle_query_mem_spaces_c,
        .cmd = QUERY_ARG_MEMORY QUERY_CONSEQUTIVE,
    };
    strcpy(query_mem_spaces_c.schema, (char[2]) { (char) ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_mem_spaces_c;
    cmd_number++;

    MCDCmdParseEntry query_reg_groups_f = {
        .handler = handle_query_reg_groups_f,
        .cmd = QUERY_ARG_REGGROUP QUERY_FIRST,
    };
    strcpy(query_reg_groups_f.schema, (char[2]) { (char) ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_reg_groups_f;
    cmd_number++;

    MCDCmdParseEntry query_reg_groups_c = {
        .handler = handle_query_reg_groups_c,
        .cmd = QUERY_ARG_REGGROUP QUERY_CONSEQUTIVE,
    };
    strcpy(query_reg_groups_c.schema, (char[2]) { (char) ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_reg_groups_c;
    cmd_number++;

    MCDCmdParseEntry query_regs_f = {
        .handler = handle_query_regs_f,
        .cmd = QUERY_ARG_REG QUERY_FIRST,
    };
    strcpy(query_regs_f.schema, (char[2]) { (char) ARG_SCHEMA_CORENUM, '\0' });
    mcd_query_cmds_table[cmd_number] = query_regs_f;
    cmd_number++;

    MCDCmdParseEntry query_regs_c = {
        .handler = handle_query_regs_c,
        .cmd = QUERY_ARG_REG QUERY_CONSEQUTIVE,
    };
    strcpy(query_regs_c.schema, (char[2]) { (char) ARG_SCHEMA_QRYHANDLE, '\0' });
    mcd_query_cmds_table[cmd_number] = query_regs_c;
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
    //might wann add tracing later (no idea for what this is used)
    //trace_gdbstub_op_start(device);

    char mcdstub_device_name[128];
    Chardev *chr = NULL;
    //Chardev *mon_chr;

    if (!first_cpu) {
        error_report("mcdstub: meaningless to attach to a "
                     "machine without any CPU.");
        return -1;
    }

    //
    if (!mcd_supports_guest_debug()) {
        error_report("mcdstub: current accelerator doesn't "
                     "support guest debugging");
        return -1;
    }

    if (!device) {
        return -1;
    }

    //if device == default -> set device = tcp::1235
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

        /* Initialize a monitor terminal for mcd */
        //mon_chr = qemu_chardev_new(NULL, TYPE_CHARDEV_MCD, NULL, NULL, &error_abort);
        //monitor_init_hmp(mon_chr, false, &error_abort);
    } else {
        qemu_chr_fe_deinit(&mcdserver_system_state.chr, true);
        //mon_chr = mcdserver_system_state.mon_chr;
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
    //mcdserver_system_state.mon_chr = mon_chr;
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
    }
}

void mcd_read_byte(uint8_t ch)
{
    uint8_t reply;

    if (mcdserver_state.last_packet->len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == TCP_NOT_ACKNOWLEDGED) {
            //the previous packet was not akcnowledged
            //trace_gdbstub_err_got_nack();
            //we are resending the last packet
            mcd_put_buffer(mcdserver_state.last_packet->data, mcdserver_state.last_packet->len);
        }
        else if (ch == TCP_ACKNOWLEDGED) {
            //the previous packet was acknowledged
            //trace_gdbstub_io_got_ack();
        }

        if (ch == TCP_ACKNOWLEDGED || ch == TCP_COMMAND_START) {
            //either acknowledged or a new communication starts -> we discard previous communication
            g_byte_array_set_size(mcdserver_state.last_packet, 0);
        }
        if (ch != TCP_COMMAND_START) {
            // we only continue if we are processing a new commant. otherwise we skip to ne next character in the packet which sould be a $
            return;
        }
    }
    if (runstate_is_running()) {
        /* when the CPU is running, we cannot do anything except stop
           it when receiving a char */
        vm_stop(RUN_STATE_PAUSED);
    }
    else {
        switch(mcdserver_state.state) {
        case RS_IDLE:
            if (ch == TCP_COMMAND_START) {
                /* start of command packet */
                mcdserver_state.line_buf_index = 0;
                mcdserver_state.line_sum = 0;
                mcdserver_state.state = RS_GETLINE;
            }
            else {
                //new communication has to start with a $
                //trace_gdbstub_err_garbage(ch);
            }
            break;
        case RS_GETLINE:
            if (ch == TCP_COMMAND_END) {
                /* end of command, start of checksum*/
                mcdserver_state.line_buf[mcdserver_state.line_buf_index++] = 0;
                //mcdserver_state.line_sum += ch;
                mcdserver_state.state = RS_DATAEND;
            }
            else if (mcdserver_state.line_buf_index >= sizeof(mcdserver_state.line_buf) - 1) {
                //the input string is too long for the linebuffer!
                //trace_gdbstub_err_overrun();
                mcdserver_state.state = RS_IDLE;
            }
            else {
                /* unescaped command character */
                //this means the character is part of the real content fo the packet and we copy it to the line_buf
                mcdserver_state.line_buf[mcdserver_state.line_buf_index++] = ch;
                mcdserver_state.line_sum += ch;
            }
            break;
        case RS_DATAEND:
            // we are now done with copying the data and in the suffix of the packet
            // TODO: maybe wanna implement a checksum or sth like the gdb protocol has

            if (ch == TCP_WAS_NOT_LAST) {
                // ~ indicates that there is an additional package coming
                //acknowledged -> send +
                reply = TCP_ACKNOWLEDGED;
                mcd_put_buffer(&reply, 1);
                mcdserver_state.state = mcd_handle_packet(mcdserver_state.line_buf);
            }
            else if (ch == TCP_WAS_LAST) {
                //acknowledged -> send +
                // | indicates that there is no additional package coming
                reply = TCP_ACKNOWLEDGED;
                mcd_put_buffer(&reply, 1);
                mcdserver_state.state = mcd_handle_packet(mcdserver_state.line_buf);
            }
            else {
                //trace_gdbstub_err_checksum_incorrect(mcdserver_state.line_sum, mcdserver_state.line_csum);
                //not acknowledged -> send -
                reply = TCP_NOT_ACKNOWLEDGED;
                mcd_put_buffer(&reply, 1);
                //waiting for package to get resent
                mcdserver_state.state = RS_IDLE;
            }
            break;
        default:
            abort();
        }
    }
}

int mcd_handle_packet(const char *line_buf)
{
    // decides what function (handler) to call depending on what the first character in the line_buf is!
    const MCDCmdParseEntry *cmd_parser = NULL;

    switch (line_buf[0]) {
    case TCP_CHAR_OPEN_SERVER:
        // handshake and lookup initialization
        {
            static MCDCmdParseEntry open_server_cmd_desc = {
                .handler = handle_open_server,
            };
            open_server_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_OPEN_SERVER, '\0' };
            cmd_parser = &open_server_cmd_desc;
        }
        break;
    case TCP_CHAR_GO:
        // go command
        {
            static MCDCmdParseEntry go_cmd_desc = {
                .handler = handle_continue,
            };
            go_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_GO, '\0' };
            cmd_parser = &go_cmd_desc;
        }
        break;
    case TCP_CHAR_KILLQEMU:
        // kill qemu completely
        error_report("QEMU: Terminated via MCDstub");
        mcd_exit(0);
        exit(0);
    case TCP_CHAR_QUERY:
        //query inquiry
        {
            static MCDCmdParseEntry query_cmd_desc = {
                .handler = handle_gen_query,
            };
            query_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_QUERY, '\0' };
            strcpy(query_cmd_desc.schema, (char[2]) { (char) ARG_SCHEMA_STRING, '\0' });
            cmd_parser = &query_cmd_desc;
        }
        break;
    case TCP_CHAR_OPEN_CORE:
        {
            static MCDCmdParseEntry open_core_cmd_desc = {
                .handler = handle_open_core,
            };
            open_core_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_OPEN_CORE, '\0' };
            strcpy(open_core_cmd_desc.schema, (char[2]) { (char) ARG_SCHEMA_CORENUM, '\0' });
            cmd_parser = &open_core_cmd_desc;
        }
        break;
    case TCP_CHAR_CLOSE_SERVER:
        {
            static MCDCmdParseEntry close_server_cmd_desc = {
                .handler = handle_close_server,
            };
            close_server_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_CLOSE_SERVER, '\0' };
            cmd_parser = &close_server_cmd_desc;
        }
        break;
    case TCP_CHAR_CLOSE_CORE:
        {
            static MCDCmdParseEntry close_core_cmd_desc = {
                .handler = handle_close_core,
            };
            close_core_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_CLOSE_CORE, '\0' };
            strcpy(close_core_cmd_desc.schema, (char[2]) { (char) ARG_SCHEMA_CORENUM, '\0' });
            cmd_parser = &close_core_cmd_desc;
        }
        break;
    default:
        // could not perform the command (because its unknown)
        mcd_put_packet("");
        break;
    }

    if (cmd_parser) {
        // now parse commands and run the selected function (handler)
        run_cmd_parser(line_buf, cmd_parser);
    }

    return RS_IDLE;
}

void handle_continue(GArray *params, void *user_ctx)
{
    /*
    if (params->len) {
        gdb_set_cpu_pc(get_param(params, 0)->val_ull);
    }

    mcdserver_state.signal = 0;
    gdb_continue();
    */
}

void handle_gen_query(GArray *params, void *user_ctx)
{
    if (!params->len) {
        return;
    }
    //now iterate over all possible query functions and execute the right one
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

    /* In case there was an error during the command parsing we must
    * send a NULL packet to indicate the command is not supported */
    if (process_string_cmd(NULL, data, cmd, 1)) {
        mcd_put_packet("");
    }
}

int cmd_parse_params(const char *data, const char *schema, GArray *params) {
    MCDCmdVariant this_param;

    char data_buffer[64] = {0};
    if (schema[0] == ARG_SCHEMA_STRING) {
        this_param.data = data;
        g_array_append_val(params, this_param);
    }
    else if (schema[0] == ARG_SCHEMA_QRYHANDLE) {
        strncat(data_buffer, data, strlen(data));
        this_param.query_handle = atoi(data_buffer);
        g_array_append_val(params, this_param);
    }
    else if (schema[0] == ARG_SCHEMA_CORENUM) {
        strncat(data_buffer, data, strlen(data));
        this_param.cpu_id = atoi(data_buffer);
        g_array_append_val(params, this_param);
    }

    return 0;
}

int process_string_cmd(void *user_ctx, const char *data, const MCDCmdParseEntry *cmds, int num_cmds)
{
    int i;
    g_autoptr(GArray) params = g_array_new(false, true, sizeof(MCDCmdVariant));

    if (!cmds) {
        return -1;
    }

    for (i = 0; i < num_cmds; i++) {
        const MCDCmdParseEntry *cmd = &cmds[i];
        //terminate if we don't have handler and cmd
        g_assert(cmd->handler && cmd->cmd);

        // if data and command are different continue
        if (strncmp(data, cmd->cmd, strlen(cmd->cmd))) {
            continue;
        }

        // if a schema is provided we need to extract parameters from the data string
        if (cmd->schema) {
            // this only gets the data from data beginning after the command name
            if (cmd_parse_params(&data[strlen(cmd->cmd)], cmd->schema, params)) {
                return -1;
            }
        }

        // the correct handler function is called
        cmd->handler(params, user_ctx);
        return 0;
    }

    return -1;
}

void mcd_exit(int code)
{
    char buf[4];

    if (!mcdserver_state.init) {
        return;
    }

    //trace_gdbstub_op_exiting((uint8_t)code);

    //need to check what is sent here and dapt it to my needs
    snprintf(buf, sizeof(buf), "W%02x", (uint8_t)code);
    mcd_put_packet(buf);

    qemu_chr_fe_deinit(&mcdserver_system_state.chr, true);
}

void mcd_chr_event(void *opaque, QEMUChrEvent event)
{
    int i;
    MCDState *s = (MCDState *) opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        // Start with first process attached, others detached
        for (i = 0; i < s->process_num; i++) {
            s->processes[i].attached = !i;
        }

        s->c_cpu = mcd_first_attached_cpu();

        vm_stop(RUN_STATE_PAUSED);
		//TODO: this might not be necessary
        //replay_gdb_attached();
        //gdb_has_xml = false;
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
	printf("this calls state_change\n");
}

int mcd_put_packet(const char *buf)
{
	//tracing
    //trace_gdbstub_io_reply(buf);

    return mcd_put_packet_binary(buf, strlen(buf), false);
}

void mcd_put_strbuf(void)
{
    mcd_put_packet(mcdserver_state.str_buf->str);
}

int mcd_put_packet_binary(const char *buf, int len, bool dump)
{
    for(;;) {
        //super interesting if we want a chekcsum or something like that here!!
        g_byte_array_set_size(mcdserver_state.last_packet, 0);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "$", 1);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) buf, len);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "#", 1);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "|", 1);

        mcd_put_buffer(mcdserver_state.last_packet->data, mcdserver_state.last_packet->len);

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
    /*
     * XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks
     */
    qemu_chr_fe_write_all(&mcdserver_system_state.chr, buf, len);
}

void mcd_set_stop_cpu(CPUState *cpu)
{
    MCDProcess *p = mcd_get_cpu_process(cpu);

    if (!p->attached) {
        /*
         * Having a stop CPU corresponding to a process that is not attached
         * confuses GDB. So we ignore the request.
         */
        return;
    }
    //FIXME: we probably can delete this because in the opern_core function we set these two anyway
    mcdserver_state.c_cpu = cpu;
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
    // TODO: maybe +1 because we start numbering at 1
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

CPUState* mcd_get_cpu(uint32_t i_cpu_index) {
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
    // TODO: maybe plus 1 because we start numbering at 1
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


void parse_reg_xml(const char *xml, int size, GArray* registers) {
    // iterates over the complete xml file
    int i, j;
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

        if (still_to_skip>0) {
            // skip chars unwanted chars
            still_to_skip --;
            continue;
        }

        if (strncmp(&xml[i], "<reg", 4)==0) {
            // start of a register
            still_to_skip = 3;
            is_reg = true;
            reg_data = g_array_new(false, true, sizeof(xml_attrib));
        }
        else if (is_reg) {
            if (strncmp(&xml[i], "/>", 2)==0) {
                // end of register info
                still_to_skip = 1;
                is_reg = false;

                // create empty register
                mcd_reg_st my_register = (const struct mcd_reg_st){ 0 };

                // add found attribtues
                for (j = 0; j<reg_data->len; j++) {
                    attribute_j = g_array_index(reg_data, xml_attrib, j);

                    argument_j = attribute_j.argument;
                    value_j = attribute_j.value;

                    if (strcmp(argument_j, "name")==0) {
                        strcpy(my_register.name, value_j);
                    }
                    else if (strcmp(argument_j, "regnum")==0) {
                        my_register.id = atoi(value_j);
                    }
                    else if (strcmp(argument_j, "bitsize")==0) {
                        my_register.bitsize = atoi(value_j);
                    }
                    else if (strcmp(argument_j, "type")==0) {
                        strcpy(my_register.type, value_j);
                    }
                    else if (strcmp(argument_j, "group")==0) {
                        strcpy(my_register.group, value_j);
                    }
                }
                // store register
                g_array_append_vals(registers, (gconstpointer)&my_register, 1);
                // free memory
                g_array_free(reg_data, false);
            }
            else {
                // store info for register
                switch (c) {
                    case ' ':
                        break;
                    case '=':
                        is_argument = false;
                        break;
                    case '"':
                        if (is_value) {
                            // end of value reached
                            is_value = false;
                            // store arg-val combo
                            xml_attrib current_attribute;
                            strcpy(current_attribute.argument, argument);
                            strcpy(current_attribute.value, value);
                            g_array_append_vals(reg_data, (gconstpointer)&current_attribute, 1);
                            memset(argument, 0, sizeof(argument));
                            memset(value, 0, sizeof(value));
                        }
                        else {
                            //start of value
                            is_value = true;
                        }
                        break;
                    default:
                        if (is_argument) {
                            strncat(argument, c_ptr, 1);
                        }
                        else if (is_value) {
                            strncat(value, c_ptr, 1);
                        }
                        else {
                            is_argument = true;
                            strncat(argument, c_ptr, 1);
                        }
                        break;
                }
            }
        }
    }
}

int int_cmp(gconstpointer a, gconstpointer b) {
    int a_int = *(int*)a;
    int b_int = *(int*)b;
    if (a_int == b_int) {
        return 0;
    }
    else {
        return 1;
    }
}

int mcd_arm_store_mem_spaces(CPUState *cpu, GArray* memspaces) {
    int nr_address_spaces = cpu->num_ases;

    mcd_mem_space_st space1 = {
        .name = "Non Secure",
        .id = 1,
        .type = 34,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&space1, 1);

    mcd_mem_space_st space2 = {
        .name = "Physical (Non Secure)",
        .id = 2,
        .type = 18,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&space2, 1);

    if (nr_address_spaces==2) {
        mcd_mem_space_st space3 = {
        .name = "Secure",
        .id = 3,
        .type = 34,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&space3, 1);
    mcd_mem_space_st space4 = {
        .name = "Physical (Secure)",
        .id = 4,
        .type = 18,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&space4, 1);
    }
    // TODO: get dynamically how the per (CP15) space is called
    mcd_mem_space_st space5 = {
        .name = "GPR Registers",
        .id = 5,
        .type = 1,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&space5, 1);
    mcd_mem_space_st space6 = {
        .name = "CP15 Registers",
        .id = 6,
        .type = 1,
        .bits_per_mau = 8,
        .invariance = 1,
        .endian = 1,
        .min_addr = 0,
        .max_addr = -1,
        .supported_access_options = 0,
    };
    g_array_append_vals(memspaces, (gconstpointer)&space6, 1);

    return 0;
}

int init_resets(GArray* resets) {
    mcd_reset_st system_reset = { .id = 0, .name = RESET_SYSTEM};
    mcd_reset_st gpr_reset = { .id = 1, .name = RESET_GPR};
    mcd_reset_st memory_reset = { .id = 2, .name = RESET_MEMORY};
    g_array_append_vals(resets, (gconstpointer)&system_reset, 1);
    g_array_append_vals(resets, (gconstpointer)&gpr_reset, 1);
    g_array_append_vals(resets, (gconstpointer)&memory_reset, 1);
    return 0;
}

int init_trigger(mcd_trigger_st* trigger) {
    trigger->type = (MCD_TRIG_TYPE_IP | MCD_TRIG_TYPE_READ | MCD_TRIG_TYPE_WRITE | MCD_TRIG_TYPE_RW);
    trigger->option = (MCD_TRIG_OPT_DATA_IS_CONDITION);
    trigger->action = (MCD_TRIG_ACTION_DBG_DEBUG);
    trigger->nr_trigger = 4;
    return 0;
}

void handle_open_server(GArray *params, void *user_ctx) {
    // initialize some core-independent data
    int return_value = 0;
    mcdserver_state.resets = g_array_new(false, true, sizeof(mcd_reset_st));
    return_value = init_resets(mcdserver_state.resets);
    if (return_value!=0) assert(0);
    return_value = init_trigger(&mcdserver_state.trigger);
    if (return_value!=0) assert(0);

    mcd_put_packet(TCP_HANDSHAKE_SUCCESS); 
}

void handle_query_system(GArray *params, void *user_ctx) {
    mcd_put_packet(MCD_SYSTEM_NAME);
}

void handle_query_cores(GArray *params, void *user_ctx) {
    //TODO: add cluster support: in gdb each inferior (process) handles one cluster we might want to have sth similar here

    // get first cpu
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
    TCP_ARGUMENT_DEVICE, device_name, TCP_ARGUMENT_CORE, cpu_model, TCP_ARGUMENT_AMOUNT_CORE, nr_cores);
    mcd_put_strbuf();
    g_free(arch);
}

int mcd_arm_parse_core_xml_file(CPUClass *cc, GArray* reggroups, GArray* registers, int* current_group_id) {
    const char *xml_filename = NULL;
    const char *current_xml_filename = NULL;
    const char *xml_content = NULL;
    int i = 0;

    // 1. get correct file
    xml_filename = cc->gdb_core_xml_file;
    for (i = 0; ; i++) {
        current_xml_filename = xml_builtin[i][0];
        if (!current_xml_filename || (strncmp(current_xml_filename, xml_filename, strlen(xml_filename)) == 0
            && strlen(current_xml_filename) == strlen(xml_filename)))
        break;
    }
    // without gpr registers we can do nothing
    if (!current_xml_filename) {
        return -1;
    }

    // 2. add group for gpr registers
    mcd_reg_group_st gprregs = { .name = "GPR Registers", .id = *current_group_id };
    g_array_append_vals(reggroups, (gconstpointer)&gprregs, 1);
    *current_group_id = *current_group_id + 1;

    // 3. parse xml
    xml_content = xml_builtin[i][1];
    parse_reg_xml(xml_content, strlen(xml_content), registers);
    return 0;
}

int mcd_arm_parse_general_xml_files(CPUState *cpu, GArray* reggroups, GArray* registers, int* current_group_id) {
    const char *xml_filename = NULL;
    const char *current_xml_filename = NULL;
    const char *xml_content = NULL;
    int i = 0;

    // iterate over all gdb xml files 
    GDBRegisterState *r;
    for (r = cpu->gdb_regs; r; r = r->next) {
        xml_filename = r->xml;
        xml_content = NULL;

        // 1. get xml content
        xml_content = arm_mcd_get_dynamic_xml(cpu, xml_filename);
        if (xml_content) {
            if (strcmp(xml_filename, "system-registers.xml")==0) {
                // these are the coprocessor register
                mcd_reg_group_st corprocessorregs = { .name = "CP15 Registers", .id = *current_group_id };
                g_array_append_vals(reggroups, (gconstpointer)&corprocessorregs, 1);
                *current_group_id = *current_group_id + 1;
            }  
        }
        else {
            // its not a coprocessor xml -> it is a static xml file
            for (i = 0; ; i++) {
                current_xml_filename = xml_builtin[i][0];
                if (!current_xml_filename || (strncmp(current_xml_filename, xml_filename, strlen(xml_filename)) == 0
                    && strlen(current_xml_filename) == strlen(xml_filename)))
                break;
            }
            if (current_xml_filename) {
                xml_content = xml_builtin[i][1];
            }
            else {
                printf("no data found for %s\n", xml_filename);
                continue;
            }
        }
        // 2. parse xml
        parse_reg_xml(xml_content, strlen(xml_content), registers);
    }
    return 0;
}

int mcd_arm_get_additional_register_info(GArray* reggroups, GArray* registers) {
    GList *register_numbers = NULL;
    mcd_reg_st *current_register;
    int i = 0;
    int id_neg_offset = 0;
    int effective_id = 0;

    // iterate over all registers
    for (i = 0; i < registers->len; i++) {
        current_register = &(g_array_index(registers, mcd_reg_st, i));
        // 1. ad the id
        if (current_register->id) {
            // id is already in place
            // NOTE: qemu doesn't emulate the FPA regs (so we are missing the indices 16 to 24)
            int used_id = current_register->id;
            register_numbers = g_list_append(register_numbers, &used_id);
            id_neg_offset ++;
        }
        else {
            effective_id = i - id_neg_offset;
            if (g_list_find_custom(register_numbers, &effective_id, (GCompareFunc)int_cmp)!=NULL) {
                id_neg_offset --;
            }
            current_register->id = i - id_neg_offset;
        }
        // 2. add mcd_reg_group_id and mcd_mem_space_id
        if (strcmp(current_register->group, "cp_regs")==0) {
            // coprocessor registers
            current_register->mcd_reg_group_id = 2;
            current_register->mcd_mem_space_id = 6;
            // TODO: get info for opcode
        }
        else {
            // gpr register
            current_register->mcd_reg_group_id = 1;
            current_register->mcd_mem_space_id = 5;
        }
    }
    g_list_free(register_numbers);
    return 0;
}

void handle_open_core(GArray *params, void *user_ctx) {
    // get the cpu whith the given id
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    CPUState *cpu = mcd_get_cpu(cpu_id);
    CPUClass *cc = CPU_GET_CLASS(cpu);
    gchar *arch = cc->gdb_arch_name(cpu);
    int return_value = 0;

    // prepare data strucutures
    GArray* memspaces = g_array_new(false, true, sizeof(mcd_mem_space_st));
    GArray* reggroups = g_array_new(false, true, sizeof(mcd_reg_group_st));
    GArray* registers = g_array_new(false, true, sizeof(mcd_reg_st));
    
    if (strcmp(arch, "arm")==0) {
        // TODO: make group and memspace ids dynamic
        int current_group_id = 1;
        // 1. store mem spaces
        return_value = mcd_arm_store_mem_spaces(cpu, memspaces);
        if (return_value!=0) assert(0);
        // 2. parse core xml
        return_value = mcd_arm_parse_core_xml_file(cc, reggroups, registers, &current_group_id);
        if (return_value!=0) assert(0);
        // 3. parse other xmls
        return_value = mcd_arm_parse_general_xml_files(cpu, reggroups, registers, &current_group_id);
        if (return_value!=0) assert(0);
        // 4. add additional data the the regs from the xmls
        return_value = mcd_arm_get_additional_register_info(reggroups, registers);
        if (return_value!=0) assert(0);
        // 5. store all found data
        if (g_list_nth(mcdserver_state.all_memspaces, cpu_id)) {
            GList* memspaces_ptr = g_list_nth(mcdserver_state.all_memspaces, cpu_id);
            memspaces_ptr->data = memspaces;
        }
        else {
            mcdserver_state.all_memspaces = g_list_insert(mcdserver_state.all_memspaces, memspaces, cpu_id);
        }
        if (g_list_nth(mcdserver_state.all_reggroups, cpu_id)) {
            GList* reggroups_ptr = g_list_nth(mcdserver_state.all_reggroups, cpu_id);
            reggroups_ptr->data = reggroups;
        }
        else {
            mcdserver_state.all_reggroups = g_list_insert(mcdserver_state.all_reggroups, reggroups, cpu_id);
        }
        if (g_list_nth(mcdserver_state.all_registers, cpu_id)) {
            GList* registers_ptr = g_list_nth(mcdserver_state.all_registers, cpu_id);
            registers_ptr->data = registers;
        }
        else {
            mcdserver_state.all_registers = g_list_insert(mcdserver_state.all_registers, registers, cpu_id);
        }
    }
    else {
        // we don't support other architectures
        assert(0);
    }
    g_free(arch);
}

void handle_query_reset_f(GArray *params, void *user_ctx) {
    // resetting has to be done over a monitor (look ar Rcmd) so we tell MCD that we can reset but this still need to be implemented
    // we only support one reset over this monitor and that would be a full "system_restart"
    // reset options are the same for every cpu!
    
    // 1. check length
    int nb_resets = mcdserver_state.resets->len;
    if (nb_resets == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    // 2. send data
    mcd_reset_st reset = g_array_index(mcdserver_state.resets, mcd_reset_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%d.", TCP_ARGUMENT_NAME, reset.name, TCP_ARGUMENT_ID, reset.id);
    mcd_put_strbuf();
    // TODO: we still need to implement the gpr and memory reset here!
}

void handle_query_reset_c(GArray *params, void *user_ctx) {
    // reset options are the same for every cpu!
    int query_index = get_param(params, 0)->query_handle;
    
    // 1. check weather this was the last mem space
    int nb_groups = mcdserver_state.resets->len;
    if (query_index+1 == nb_groups) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 2. send data
    mcd_reset_st reset = g_array_index(mcdserver_state.resets, mcd_reset_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%d.", TCP_ARGUMENT_NAME, reset.name, TCP_ARGUMENT_ID, reset.id);
    mcd_put_strbuf();
    // TODO: we still need to implement the gpr and memory reset here!
}

void handle_close_core(GArray *params, void *user_ctx) {
    // free memory for correct core
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    GArray* memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);
    g_array_free(memspaces, TRUE);
    GArray* reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);
    g_array_free(reggroups, TRUE);
    GArray* registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);
    g_array_free(registers, TRUE);
}

void handle_close_server(GArray *params, void *user_ctx) {
    uint32_t pid = 1;
    MCDProcess *process = mcd_get_process(pid);

    // 1. free memory
    // TODO: do this only if there are no processes attached anymore!
    g_list_free(mcdserver_state.all_memspaces);
    g_list_free(mcdserver_state.all_reggroups);
    g_list_free(mcdserver_state.all_registers);
    g_array_free(mcdserver_state.resets, TRUE);

    // 2. detach
    process->attached = false;

    // 3. reset process
    if (pid == mcd_get_cpu_pid(mcdserver_state.c_cpu)) {
        mcdserver_state.c_cpu = mcd_first_attached_cpu();
    }
    if (!mcdserver_state.c_cpu) {
        /* No more process attached */
        mcd_disable_syscalls();
        mcd_continue();
    }
}

void handle_query_trigger(GArray *params, void *user_ctx) {
    mcd_trigger_st trigger = mcdserver_state.trigger;
    g_string_printf(mcdserver_state.str_buf, "%s=%d.%s=%d.%s=%d.%s=%d.",
        TCP_ARGUMENT_AMOUNT_TRIGGER,  trigger.nr_trigger, TCP_ARGUMENT_TYPE, trigger.type,
        TCP_ARGUMENT_OPTION, trigger.option, TCP_ARGUMENT_ACTION, trigger.action);
    mcd_put_strbuf();
}

void mcd_continue(void)
{
    if (!runstate_needs_reset()) {
        vm_start();
    }
}

void handle_query_mem_spaces_f(GArray *params, void *user_ctx) {
    // 1. get correct memspaces and set the query_cpu
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray* memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);

    // 2. check length
    int nb_groups = memspaces->len;
    if (nb_groups == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }

    // 3. send data
    mcd_mem_space_st space = g_array_index(memspaces, mcd_mem_space_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%d.%s=%d.%s=%d.%s=%d.%s=%d.%s=%ld.%s=%ld.%s=%d.",
        TCP_ARGUMENT_NAME, space.name, TCP_ARGUMENT_ID, space.id, TCP_ARGUMENT_TYPE, space.type,
        TCP_ARGUMENT_BITS_PER_MAU, space.bits_per_mau, TCP_ARGUMENT_INVARIANCE, space.invariance, TCP_ARGUMENT_ENDIAN, space.endian,
        TCP_ARGUMENT_MIN, space.min_addr, TCP_ARGUMENT_MAX, space.max_addr,
        TCP_ARGUMENT_SUPPORTED_ACCESS_OPTIONS, space.supported_access_options);
    mcd_put_strbuf();
}

void handle_query_mem_spaces_c(GArray *params, void *user_ctx) {
    // this funcitons send all mem spaces except for the first
    // 1. get parameter and memspace
    int query_index = get_param(params, 0)->query_handle;
    uint32_t cpu_id = mcdserver_state.query_cpu_id;
    GArray* memspaces = g_list_nth_data(mcdserver_state.all_memspaces, cpu_id);

    // 2. check weather this was the last mem space
    int nb_groups = memspaces->len;
    if (query_index+1 == nb_groups) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 3. send the correct memspace
    mcd_mem_space_st space = g_array_index(memspaces, mcd_mem_space_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%s.%s=%d.%s=%d.%s=%d.%s=%d.%s=%d.%s=%ld.%s=%ld.%s=%d.",
        TCP_ARGUMENT_NAME, space.name, TCP_ARGUMENT_ID, space.id, TCP_ARGUMENT_TYPE, space.type,
        TCP_ARGUMENT_BITS_PER_MAU, space.bits_per_mau, TCP_ARGUMENT_INVARIANCE, space.invariance, TCP_ARGUMENT_ENDIAN, space.endian,
        TCP_ARGUMENT_MIN, space.min_addr, TCP_ARGUMENT_MAX, space.max_addr,
        TCP_ARGUMENT_SUPPORTED_ACCESS_OPTIONS, space.supported_access_options);
    mcd_put_strbuf();
}

void handle_query_reg_groups_f(GArray *params, void *user_ctx) {
    // 1. get correct reggroups and set the query_cpu
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray* reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);

    // 2. check length
    int nb_groups = reggroups->len;
    if (nb_groups == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    // 3. send data
    mcd_reg_group_st group = g_array_index(reggroups, mcd_reg_group_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%d.%s=%s.", TCP_ARGUMENT_ID, group.id, TCP_ARGUMENT_NAME, group.name);
    mcd_put_strbuf();
}

void handle_query_reg_groups_c(GArray *params, void *user_ctx) {
    // this funcitons send all reg groups except for the first
    // 1. get parameter and memspace
    int query_index = get_param(params, 0)->query_handle;
    uint32_t cpu_id = mcdserver_state.query_cpu_id;
    GArray* reggroups = g_list_nth_data(mcdserver_state.all_reggroups, cpu_id);

    // 2. check weather this was the last reg group
    int nb_groups = reggroups->len;
    if (query_index+1 == nb_groups) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 3. send the correct reggroup
    mcd_reg_group_st group = g_array_index(reggroups, mcd_reg_group_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%d.%s=%s.", TCP_ARGUMENT_ID, group.id, TCP_ARGUMENT_NAME, group.name);
    mcd_put_strbuf();
}

void handle_query_regs_f(GArray *params, void *user_ctx) {
    // 1. get correct registers and set the query_cpu
    uint32_t cpu_id = get_param(params, 0)->cpu_id;
    mcdserver_state.query_cpu_id = cpu_id;
    GArray* registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);

    // 2. check length
    int nb_regs = registers->len;
    if (nb_regs == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    // 3. send data
    mcd_reg_st my_register = g_array_index(registers, mcd_reg_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%d.%s=%s.%s=%d.%s=%d.%s=%d.%s=%d.%s=%d.",
        TCP_ARGUMENT_ID, my_register.id, TCP_ARGUMENT_NAME,  my_register.name, TCP_ARGUMENT_SIZE, my_register.bitsize,
        TCP_ARGUMENT_REGGROUPID, my_register.mcd_reg_group_id, TCP_ARGUMENT_MEMSPACEID, my_register.mcd_mem_space_id,
        TCP_ARGUMENT_TYPE, my_register.mcd_reg_type, TCP_ARGUMENT_THREAD, my_register.mcd_hw_thread_id);
    mcd_put_strbuf();
}

void handle_query_regs_c(GArray *params, void *user_ctx) {
    // this funcitons send all reg groups except for the first
    // 1. get parameter and registers
    int query_index = get_param(params, 0)->query_handle;
    uint32_t cpu_id = mcdserver_state.query_cpu_id;
    GArray* registers = g_list_nth_data(mcdserver_state.all_registers, cpu_id);

    // 2. check weather this was the last register
    int nb_regs = registers->len;
    if (query_index+1 == nb_regs) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 3. send the correct register
    mcd_reg_st my_register = g_array_index(registers, mcd_reg_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "%s=%d.%s=%s.%s=%d.%s=%d.%s=%d.%s=%d.%s=%d.",
        TCP_ARGUMENT_ID, my_register.id, TCP_ARGUMENT_NAME,  my_register.name, TCP_ARGUMENT_SIZE, my_register.bitsize,
        TCP_ARGUMENT_REGGROUPID, my_register.mcd_reg_group_id, TCP_ARGUMENT_MEMSPACEID, my_register.mcd_mem_space_id,
        TCP_ARGUMENT_TYPE, my_register.mcd_reg_type, TCP_ARGUMENT_THREAD, my_register.mcd_hw_thread_id);
    mcd_put_strbuf();
}
