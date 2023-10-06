/*
 * This is the main mcdstub. It needs to be complemented by other mcd stubs for each target.
 */

//from original gdbstub.c
#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
//#include "trace.h"
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
#include "hw/core/cpu.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "monitor/monitor.h"

// just used for the xml_builtin stuff
//#include "exec/gdbstub.h"       /* xml_builtin */

typedef struct {
    CharBackend chr;
    //Chardev *mon_chr;
} MCDSystemState;

MCDSystemState mcdserver_system_state;

MCDState mcdserver_state;

static const MCDCmdParseEntry mcd_gen_query_table[] = {
    // this is a list of all query commands. it gets iterated over only the handler of the matching command will get executed
    {
        .handler = handle_query_system,
        .cmd = "system",
    },
    {
        .handler = handle_query_cores,
        .cmd = "cores",
    },
    {
        .handler = handle_query_reset,
        .cmd = "reset",
    },
};

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
        if (ch == '-') {
            //the previous packet was not akcnowledged
            //trace_gdbstub_err_got_nack();
            //we are resending the last packet
            mcd_put_buffer(mcdserver_state.last_packet->data, mcdserver_state.last_packet->len);
        }
        else if (ch == '+') {
            //the previous packet was acknowledged
            //trace_gdbstub_io_got_ack();
        }

        if (ch == '+' || ch == '$') {
            //either acknowledged or a new communication starts -> we discard previous communication
            g_byte_array_set_size(mcdserver_state.last_packet, 0);
        }
        if (ch != '$') {
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
            if (ch == '$') {
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
            if (ch == '#') {
                /* end of command, start of checksum*/
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

            if (ch == '~') {
                // ~ indicates that there is an additional package coming
                //acknowledged -> send +
                reply = '+';
                mcd_put_buffer(&reply, 1);
                mcdserver_state.state = mcd_handle_packet(mcdserver_state.line_buf);
            }
            else if (ch == '|') {
                //acknowledged -> send +
                // | indicates that there is no additional package coming
                reply = '+';
                mcd_put_buffer(&reply, 1);
                mcdserver_state.state = mcd_handle_packet(mcdserver_state.line_buf);
            }
            else {
                //trace_gdbstub_err_checksum_incorrect(mcdserver_state.line_sum, mcdserver_state.line_csum);
                //not acknowledged -> send -
                reply = '-';
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
    //decides what function (handler) to call depending on what the first character in the line_buf is!
    const MCDCmdParseEntry *cmd_parser = NULL;

    //trace_gdbstub_io_command(line_buf);

    switch (line_buf[0]) {
    case 'i':
        //handshake
        mcd_put_packet("shaking your hand");
        //gdb_put_packet("OK");
        break;
    case 'c':
        //go command
        {
            static const MCDCmdParseEntry continue_cmd_desc = {
                .handler = handle_continue,
                .cmd = "c",
                //.cmd_startswith = 1,
                //.schema = "L0"
            };
            cmd_parser = &continue_cmd_desc;
        }
        break;
    case 'k':
        // kill qemu completely
        error_report("QEMU: Terminated via MCDstub");
        mcd_exit(0);
        exit(0);
    case 'q':
        //query inquiry
        {
            static const MCDCmdParseEntry gen_query_cmd_desc = {
                .handler = handle_gen_query,
                .cmd = "q",
                //.cmd_startswith = 1,
                .schema = "ss"
            };
            cmd_parser = &gen_query_cmd_desc;
        }
        break;
    case 'H':
        //current alias for open core, later this will probably be a part of the 'i' requests
        {
            static const MCDCmdParseEntry gen_core_open = {
                .handler = handle_core_open,
                .cmd = "H",
                //.cmd_startswith = 1,
                .schema = "ss"
            };
            cmd_parser = &gen_core_open;
        }
        break;
    case 'D':
        {
            static const MCDCmdParseEntry detach_cmd_desc = {
                .handler = handle_detach,
                .cmd = "D",
                //.cmd_startswith = 1,
                //.schema = "?.l0"
            };
            cmd_parser = &detach_cmd_desc;
        }
        break;
    default:
        //could not perform the command (because its unknown)
        mcd_put_packet("");
        break;
    }

    if (cmd_parser) {
        //now parse commands and run the selected function (handler)
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
                           mcd_gen_query_table,
                           ARRAY_SIZE(mcd_gen_query_table))) {
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

int cmd_parse_params(const char *data, const char *schema, GArray *params)
{
    MCDCmdVariant this_param;
    this_param.data = data;
    g_array_append_val(params, this_param);
    /*
    const char *curr_schema, *curr_data;

    g_assert(schema);
    g_assert(params->len == 0);

    curr_schema = schema;
    curr_data = data;
    while (curr_schema[0] && curr_schema[1] && *curr_data) {
        GdbCmdVariant this_param;

        switch (curr_schema[0]) {
        case 'l':
            if (qemu_strtoul(curr_data, &curr_data, 16,
                             &this_param.val_ul)) {
                return -EINVAL;
            }
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 'L':
            if (qemu_strtou64(curr_data, &curr_data, 16,
                              (uint64_t *)&this_param.val_ull)) {
                return -EINVAL;
            }
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 's':
            this_param.data = curr_data;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 'o':
            this_param.opcode = *(uint8_t *)curr_data;
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case 't':
            this_param.thread_id.kind =
                read_thread_id(curr_data, &curr_data,
                               &this_param.thread_id.pid,
                               &this_param.thread_id.tid);
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            g_array_append_val(params, this_param);
            break;
        case '?':
            curr_data = cmd_next_param(curr_data, curr_schema[1]);
            break;
        default:
            return -EINVAL;
        }
        curr_schema += 2;
    }
    */
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
            //currently doing nothing
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
        s->g_cpu = s->c_cpu;

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
	/*
    CPUState *cpu = mcdserver_state.c_cpu;
    g_autoptr(GString) buf = g_string_new(NULL);
    g_autoptr(GString) tid = g_string_new(NULL);
    const char *type;
    int ret;

    if (running || mcdserver_state.state == RS_INACTIVE) {
        return;
    }

    //Is there a GDB syscall waiting to be sent?
    if (gdb_handled_syscall()) {
        return;
    }

    if (cpu == NULL) {
        //No process attached
        return;
    }

    gdb_append_thread_id(cpu, tid);

    switch (state) {
    case RUN_STATE_DEBUG:
        if (cpu->watchpoint_hit) {
            switch (cpu->watchpoint_hit->flags & BP_MEM_ACCESS) {
            case BP_MEM_READ:
                type = "r";
                break;
            case BP_MEM_ACCESS:
                type = "a";
                break;
            default:
                type = "";
                break;
            }
            trace_gdbstub_hit_watchpoint(type,
                                         gdb_get_cpu_index(cpu),
                                         cpu->watchpoint_hit->vaddr);
            g_string_printf(buf, "T%02xthread:%s;%swatch:%" VADDR_PRIx ";",
                            GDB_SIGNAL_TRAP, tid->str, type,
                            cpu->watchpoint_hit->vaddr);
            cpu->watchpoint_hit = NULL;
            goto send_packet;
        } else {
            trace_gdbstub_hit_break();
        }
        tb_flush(cpu);
        ret = GDB_SIGNAL_TRAP;
        break;
    case RUN_STATE_PAUSED:
        trace_gdbstub_hit_paused();
        ret = GDB_SIGNAL_INT;
        break;
    case RUN_STATE_SHUTDOWN:
        trace_gdbstub_hit_shutdown();
        ret = GDB_SIGNAL_QUIT;
        break;
    case RUN_STATE_IO_ERROR:
        trace_gdbstub_hit_io_error();
        ret = GDB_SIGNAL_IO;
        break;
    case RUN_STATE_WATCHDOG:
        trace_gdbstub_hit_watchdog();
        ret = GDB_SIGNAL_ALRM;
        break;
    case RUN_STATE_INTERNAL_ERROR:
        trace_gdbstub_hit_internal_error();
        ret = GDB_SIGNAL_ABRT;
        break;
    case RUN_STATE_SAVE_VM:
    case RUN_STATE_RESTORE_VM:
        return;
    case RUN_STATE_FINISH_MIGRATE:
        ret = GDB_SIGNAL_XCPU;
        break;
    default:
        trace_gdbstub_hit_unknown(state);
        ret = GDB_SIGNAL_UNKNOWN;
        break;
    }
    mcd_set_stop_cpu(cpu);
    g_string_printf(buf, "T%02xthread:%s;", ret, tid->str);

send_packet:
    mcd_put_packet(buf->str);

    // disable single step if it was enabled
    cpu_single_step(cpu, 0);
*/
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
    //int csum, i;
    //uint8_t footer[3];

	//trace stuff
    //if (dump && trace_event_get_state_backends(TRACE_GDBSTUB_IO_BINARYREPLY)) {
    //    hexdump(buf, len, trace_gdbstub_io_binaryreply);
    //}

    for(;;) {
        //super interesting if we want a chekcsum or something like that here!!
        g_byte_array_set_size(mcdserver_state.last_packet, 0);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "$", 1);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) buf, len);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "#", 1);
        g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "|", 1);
        /*
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        footer[0] = '#';
        footer[1] = tohex((csum >> 4) & 0xf);
        footer[2] = tohex((csum) & 0xf);
        g_byte_array_append(mcdserver_state.last_packet, footer, 3);
        */
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
    mcdserver_state.g_cpu = cpu;
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
/*
void handle_query_first_threads(GArray *params, void *user_ctx)
{
    // chache the first cpu
    mcdserver_state.query_cpu = mcd_first_attached_cpu();
    // new answer over tcp
    handle_query_threads(params, user_ctx);
}

void handle_query_threads(GArray *params, void *user_ctx)
{
    if (!mcdserver_state.query_cpu) {
        // send packet back that that there is no more threads
        //gdb_put_packet("l");
        return;
    }

    g_string_assign(mcdserver_state.str_buf, "m");
    mcd_append_thread_id(mcdserver_state.query_cpu, mcdserver_state.str_buf);
    mcd_put_strbuf();
    mcdserver_state.query_cpu = mcd_next_attached_cpu(mcdserver_state.query_cpu);
}


void mcd_append_thread_id(CPUState *cpu, GString *buf)
{
    g_string_append_printf(buf, "p%02x.%02x", mcd_get_cpu_pid(cpu), mcd_get_cpu_index(cpu));
}
*/

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

void handle_query_system(GArray *params, void *user_ctx) {
    mcd_put_packet("qemu-system");
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
    
    //const char *cpu_name = object_get_canonical_path_component(OBJECT(cpu));
    //int process_id = mcd_get_cpu_pid(cpu);
    //int cpu_index = cpu->cpu_index;
    //int cpu_cluster = cpu->cluster_index;
    int nr_cores = cpu->nr_cores;

    g_string_append_printf(mcdserver_state.str_buf, "device=\"qemu-%s-device\",core=\"%s\",nr_cores=\"%d\"", arch, cpu_model, nr_cores);
    mcd_put_strbuf();
    g_free(arch);
}

void handle_core_open(GArray *params, void *user_ctx) {
    // get the cpu whith the given id
    uint32_t cpu_id = atoi(get_param(params, 0)->data);

    CPUState *cpu = mcd_get_cpu(cpu_id);

    // select the the cpu as the current cpu for all request from the mcd interface
    mcdserver_state.c_cpu = cpu;
    mcdserver_state.g_cpu = cpu;

}

void handle_query_reset(GArray *params, void *user_ctx) {
    // resetting has to be done over a monitor (look ar Rcmd) so we tell MCD that we can reset but this still need to be implemented
    // we only support one reset over this monitor and that would be a fully "system_restart"
    mcd_put_packet("info_rst=\"results in a full system restart\"");
}

void handle_detach(GArray *params, void *user_ctx) {
    uint32_t pid = 1;
    MCDProcess *process = mcd_get_process(pid);

    // 1. cleanup
    // gdb_process_breakpoint_remove_all(process);

    // 2. detach
    process->attached = false;

    // reset current cpus
    // TODO: if we don't use c_cpu we can delete this
    // this also checks to only reset THIS process we also probably don't need this since we only got one process!
    if (pid == mcd_get_cpu_pid(mcdserver_state.c_cpu)) {
        mcdserver_state.c_cpu = mcd_first_attached_cpu();
    }

    if (pid == mcd_get_cpu_pid(mcdserver_state.g_cpu)) {
        mcdserver_state.g_cpu = mcd_first_attached_cpu();
    }

    if (!mcdserver_state.c_cpu) {
        /* No more process attached */
        mcd_disable_syscalls();
        mcd_continue();
    }
}

void mcd_continue(void)
{
    if (!runstate_needs_reset()) {
        vm_start();
    }
}
