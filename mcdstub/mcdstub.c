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
	// this is weird and might be able to sit just like it is here with the same value as for gdb
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
    printf("incoming buffer: %s\n", buf);
    char send_buffer[] = "shaking your hand";
    mcd_put_packet(send_buffer);
    //int i;
	/*
    for (i = 0; i < size; i++) {
        //TODO: some byte reading or idk gdb_read_byte(buf[i]);
    }*/
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
        //g_byte_array_append(mcdserver_state.last_packet, (const uint8_t *) "$", 1);
        g_byte_array_append(mcdserver_state.last_packet,
                            (const uint8_t *) buf, len);
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