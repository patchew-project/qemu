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
#include "hw/core/cpu.h"
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
    {
        .handler = handle_query_trigger,
        .cmd = "trigger",
    },
    {
        .handler = handle_query_mem_spaces_f,
        .cmd = "memoryf",
    },
    {
        .handler = handle_query_mem_spaces_c,
        .cmd = "memoryc",
        .schema = ARG_SCHEMA_QRYHANDLE,
    },
    {
        .handler = handle_query_reg_groups_f,
        .cmd = "reggroupf",
    },
    {
        .handler = handle_query_reg_groups_c,
        .cmd = "reggroupc",
        .schema = ARG_SCHEMA_QRYHANDLE,
    },
    {
        .handler = handle_query_regs_f,
        .cmd = "regf",
    },
    {
        .handler = handle_query_regs_c,
        .cmd = "regc",
        .schema = ARG_SCHEMA_QRYHANDLE,
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
    case TCP_CHAR_INIT:
        // handshake and lookup initialization
        {
            static MCDCmdParseEntry init_cmd_desc = {
                .handler = handle_init,
            };
            init_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_INIT, '\0' };
            cmd_parser = &init_cmd_desc;
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
                .schema = ARG_SCHEMA_STRING
            };
            query_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_QUERY, '\0' };
            cmd_parser = &query_cmd_desc;
        }
        break;
    case TCP_CHAR_OPEN_CORE:
        {
            static MCDCmdParseEntry gen_open_core = {
                .handler = handle_open_core,
                .schema = ARG_SCHEMA_CORENUM
            };
            gen_open_core.cmd = (char[2]) { (char) TCP_CHAR_OPEN_CORE, '\0' };
            cmd_parser = &gen_open_core;
        }
        break;
    case TCP_CHAR_DETACH:
        {
            static MCDCmdParseEntry detach_cmd_desc = {
                .handler = handle_detach,
            };
            detach_cmd_desc.cmd = (char[2]) { (char) TCP_CHAR_DETACH, '\0' };
            cmd_parser = &detach_cmd_desc;
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

int cmd_parse_params(const char *data, const char *schema, GArray *params) {
    MCDCmdVariant this_param;

    char data_buffer[64] = {0};
    if (schema[0] == atoi(ARG_SCHEMA_STRING)) {
        this_param.data = data;
        g_array_append_val(params, this_param);
    }
    else if (schema[0] == atoi(ARG_SCHEMA_QRYHANDLE)) {
        strncat(data_buffer, data, strlen(data));
        this_param.query_handle = atoi(data_buffer);
        g_array_append_val(params, this_param);
    }
    else if (schema[0] == atoi(ARG_SCHEMA_CORENUM)) {
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


void parse_reg_xml(const char *xml, int size) {
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
                g_array_append_vals(mcdserver_state.registers, (gconstpointer)&my_register, 1);
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

void mcd_arm_store_mem_spaces(int nr_address_spaces) {
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
    g_array_append_vals(mcdserver_state.memspaces, (gconstpointer)&space1, 1);

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
    g_array_append_vals(mcdserver_state.memspaces, (gconstpointer)&space2, 1);

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
    g_array_append_vals(mcdserver_state.memspaces, (gconstpointer)&space3, 1);
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
    g_array_append_vals(mcdserver_state.memspaces, (gconstpointer)&space4, 1);
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
    g_array_append_vals(mcdserver_state.memspaces, (gconstpointer)&space5, 1);
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
    g_array_append_vals(mcdserver_state.memspaces, (gconstpointer)&space6, 1);
}

void handle_init(GArray *params, void *user_ctx) {
    // the mcdserver is set up and we return the handshake
    mcd_put_packet("shaking your hand"); 
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
    
    int nr_cores = cpu->nr_cores;

    g_string_append_printf(mcdserver_state.str_buf, "device=\"qemu-%s-device\",core=\"%s\",nr_cores=\"%d\"", arch, cpu_model, nr_cores);
    mcd_put_strbuf();
    g_free(arch);
}

void handle_open_core(GArray *params, void *user_ctx) {
    // get the cpu whith the given id
    uint32_t cpu_id = get_param(params, 0)->cpu_id;

    CPUState *cpu = mcd_get_cpu(cpu_id);

    CPUClass *cc = CPU_GET_CLASS(cpu);

    gchar *arch = cc->gdb_arch_name(cpu);

    // TODO: this might cause a memory leak when called a second time -> maybe free the Garray first
    mcdserver_state.memspaces = g_array_new(false, true, sizeof(mcd_mem_space_st));
    mcdserver_state.reggroups = g_array_new(false, true, sizeof(mcd_reg_group_st));
    mcdserver_state.registers = g_array_new(false, true, sizeof(mcd_reg_st));

    
    if (strcmp(arch, "arm")==0) {
        // store reg groups
        uint32_t current_group_id = 0;

        // at the moment we just assume there are 3 spaces (gpr, per and debug)
        
        // store mem spaces
        int nr_address_spaces = cpu->num_ases;
        mcd_arm_store_mem_spaces(nr_address_spaces);
        // mem spaces done


        GList *register_numbers = NULL;

        const char *xml_filename = NULL;
        const char *xml_content = NULL;
        const char *name = NULL;
        int i;

        // 1. check out the core xml file
        xml_filename = cc->gdb_core_xml_file;

        for (i = 0; ; i++) {
                name = xml_builtin[i][0];
                if (!name || (strncmp(name, xml_filename, strlen(xml_filename)) == 0 && strlen(name) == strlen(xml_filename)))
                break;
            }
        // without gpr registers we can do nothing
        assert(name);
        // add group for gpr registers
        current_group_id = 1;
        mcd_reg_group_st group1 = { .name = "GPR Registers", .id = current_group_id };
        g_array_append_vals(mcdserver_state.reggroups, (gconstpointer)&group1, 1);

        // parse xml
        xml_content = xml_builtin[i][1];
        parse_reg_xml(xml_content, strlen(xml_content));

        // 2. iterate over all other xml files
        GDBRegisterState *r;
        for (r = cpu->gdb_regs; r; r = r->next) {
            xml_filename = r->xml;
            xml_content = NULL;

            // first, check if this is a coprocessor xml

            // funciton call
            xml_content = arm_mcd_get_dynamic_xml(cpu, xml_filename);
            if (xml_content) {
                if (strcmp(xml_filename, "system-registers.xml")==0) {
                    //these are the coprocessor register
                    current_group_id = 2;
                    mcd_reg_group_st group2 = { .name = "CP15 Registers", .id = current_group_id };
                    g_array_append_vals(mcdserver_state.reggroups, (gconstpointer)&group2, 1);
                }
                
            }
            else {
                // its not a coprocessor xml -> it is a static xml file
                for (i = 0; ; i++) {
                    name = xml_builtin[i][0];
                    if (!name || (strncmp(name, xml_filename, strlen(xml_filename)) == 0 && strlen(name) == strlen(xml_filename)))
                    break;
                }
                if (name) {
                    xml_content = xml_builtin[i][1];
                }
                else {
                    printf("no data found for %s\n", xml_filename);
                    continue;
                }
            }

            // parse xml
            parse_reg_xml(xml_content, strlen(xml_content));
        }
        // go over the register array and collect all additional data
        mcd_reg_st *current_register;
        int id_neg_offset = 0;
        int effective_id;
        for (i = 0; i < mcdserver_state.registers->len; i++) {
            current_register = &(g_array_index(mcdserver_state.registers, mcd_reg_st, i));
            // ad an id handle
            if (current_register->id) {
                // id is already in place
                //FIXME: we are missing 10 registers (likely the FPA regs or sth)
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
            // sort into correct reg_group and according mem_space
            if (strcmp(current_register->group, "cp_regs")==0) {
                current_register->mcd_reg_group_id = 2;
                current_register->mcd_mem_space_id = 6;
                // get info for opcode
            }
            else {
                // gpr register
                current_register->mcd_reg_group_id = 1;
                current_register->mcd_mem_space_id = 5;
            }
        }
        // free memory
        g_list_free(register_numbers);
    }
    else {
        // we don't support other architectures
        assert(0);
    }
    g_free(arch);
}

void handle_query_reset(GArray *params, void *user_ctx) {
    // resetting has to be done over a monitor (look ar Rcmd) so we tell MCD that we can reset but this still need to be implemented
    // we only support one reset over this monitor and that would be a fully "system_restart"
    mcd_put_packet("nr=\"3\",info=\"0,full_system_reset;1,gpr_reset;2,memory_reset;\"");
    // TODO: we still need to implement the gpr and memory reset here!
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

    if (!mcdserver_state.c_cpu) {
        /* No more process attached */
        mcd_disable_syscalls();
        mcd_continue();
    }
}

void handle_query_trigger(GArray *params, void *user_ctx) {
    // set the type, option and action bitmask and send it

    uint32_t type = (MCD_TRIG_TYPE_IP | MCD_TRIG_TYPE_READ | MCD_TRIG_TYPE_WRITE | MCD_TRIG_TYPE_RW);
    uint32_t option = (MCD_TRIG_OPT_DATA_IS_CONDITION);
    uint32_t action = (MCD_TRIG_ACTION_DBG_DEBUG);
    uint32_t nr_trigger = 4;

    g_string_printf(mcdserver_state.str_buf, "nr=\"%d\",info=\"%d;%d;%d;\"", nr_trigger, type, option, action);
    mcd_put_strbuf();
}

void mcd_continue(void)
{
    if (!runstate_needs_reset()) {
        vm_start();
    }
}

void handle_query_mem_spaces_f(GArray *params, void *user_ctx) {
    // send the first mem space
    int nb_groups = mcdserver_state.memspaces->len;
    if (nb_groups == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    mcd_mem_space_st space = g_array_index(mcdserver_state.memspaces, mcd_mem_space_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "name=%s.id=%d.type=%d.bpm=%d.i=%d.e=%d.min=%ld.max=%ld.sao=%d.",
        space.name, space.id, space.type, space.bits_per_mau, space.invariance, space.endian,
        space.min_addr, space.max_addr, space.supported_access_options);
    mcd_put_strbuf();
}

void handle_query_mem_spaces_c(GArray *params, void *user_ctx) {
    // this funcitons send all mem spaces except for the first
    // 1. get parameter
    int query_index = get_param(params, 0)->query_handle;

    // 2. check weather this was the last mem space
    int nb_groups = mcdserver_state.memspaces->len;
    if (query_index+1 == nb_groups) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 3. send the correct memspace
    mcd_mem_space_st space = g_array_index(mcdserver_state.memspaces, mcd_mem_space_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "name=%s.id=%d.type=%d.bpm=%d.i=%d.e=%d.min=%ld.max=%ld.sao=%d.",
        space.name, space.id, space.type, space.bits_per_mau, space.invariance, space.endian,
        space.min_addr, space.max_addr, space.supported_access_options);
    mcd_put_strbuf();
}

void handle_query_reg_groups_f(GArray *params, void *user_ctx) {
    // send the first reg group
    int nb_groups = mcdserver_state.reggroups->len;
    if (nb_groups == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    mcd_reg_group_st group = g_array_index(mcdserver_state.reggroups, mcd_reg_group_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "id=%d.name=%s.", group.id, group.name);
    mcd_put_strbuf();
}

void handle_query_reg_groups_c(GArray *params, void *user_ctx) {
    // this funcitons send all reg groups except for the first
    // 1. get parameter
    int query_index = get_param(params, 0)->query_handle;

    // 2. check weather this was the last reg group
    int nb_groups = mcdserver_state.reggroups->len;
    if (query_index+1 == nb_groups) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 3. send the correct reggroup
    mcd_reg_group_st group = g_array_index(mcdserver_state.reggroups, mcd_reg_group_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "id=%d.name=%s.", group.id, group.name);
    mcd_put_strbuf();
}

void handle_query_regs_f(GArray *params, void *user_ctx) {
    // send the first register
    int nb_regs = mcdserver_state.registers->len;
    if (nb_regs == 1) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "1!");
    }
    mcd_reg_st my_register = g_array_index(mcdserver_state.registers, mcd_reg_st, 0);
    g_string_append_printf(mcdserver_state.str_buf, "id=%d.name=%s.size=%d.reggroupid=%d.memspaceid=%d.type=%d.thread=%d.",
        my_register.id, my_register.name, my_register.bitsize, my_register.mcd_reg_group_id,
        my_register.mcd_mem_space_id, my_register.mcd_reg_type, my_register.mcd_hw_thread_id);
    mcd_put_strbuf();
}

void handle_query_regs_c(GArray *params, void *user_ctx) {
    // this funcitons send all registers except for the first
    // 1. get parameter
    int query_index = get_param(params, 0)->query_handle;

    // 2. check weather this was the last register
    int nb_regs = mcdserver_state.registers->len;
    if (query_index+1 == nb_regs) {
        // indicates this is the last packet
        g_string_printf(mcdserver_state.str_buf, "0!");
    }
    else {
        g_string_printf(mcdserver_state.str_buf, "%d!", query_index+1);
    }

    // 3. send the correct register
    mcd_reg_st my_register = g_array_index(mcdserver_state.registers, mcd_reg_st, query_index);
    g_string_append_printf(mcdserver_state.str_buf, "id=%d.name=%s.size=%d.reggroupid=%d.memspaceid=%d.type=%d.thread=%d.",
        my_register.id, my_register.name, my_register.bitsize, my_register.mcd_reg_group_id,
        my_register.mcd_mem_space_id, my_register.mcd_reg_type, my_register.mcd_hw_thread_id);
    mcd_put_strbuf();
}
