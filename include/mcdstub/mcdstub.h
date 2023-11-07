#ifndef MCDSTUB_INTERNALS_H
#define MCDSTUB_INTERNALS_H

#include "exec/cpu-common.h"
#include "chardev/char.h"
#include "hw/core/cpu.h"
#include "mcdstub_common.h"

#define MAX_PACKET_LENGTH 1024
/* misc */
#define QUERY_TOTAL_NUMBER 12
#define CMD_SCHEMA_LENGTH 6
#define MCD_SYSTEM_NAME "qemu-system"

/* supported architectures */
#define MCDSTUB_ARCH_ARM "arm"

/* tcp query packet values templates */
#define DEVICE_NAME_TEMPLATE(s) "qemu-" #s "-device"

typedef struct MCDProcess {
    uint32_t pid;
    bool attached;

    char target_xml[1024];
} MCDProcess;

#define get_param(p, i)    (&g_array_index(p, MCDCmdVariant, i))

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_DATAEND,
};

typedef struct MCDState {
    bool init;       /* have we been initialised? */
    CPUState *c_cpu; /* current CPU for everything */
    enum RSState state; /* parsing state */
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum; /* running checksum */
    int line_csum; /* checksum at the end of the packet */
    GByteArray *last_packet;
    int signal;

    MCDProcess *processes;
    int process_num;
    GString *str_buf;
    GByteArray *mem_buf;
    int sstep_flags;
    int supported_sstep_flags;

    uint32_t query_cpu_id;
    GList *all_memspaces;
    GList *all_reggroups;
    GList *all_registers;
    GList *all_breakpoints;
    GArray *resets;
    mcd_trigger_into_st trigger;
    mcd_cpu_state_st cpu_state;
    MCDCmdParseEntry mcd_query_cmds_table[QUERY_TOTAL_NUMBER];
} MCDState;

/* lives in main mcdstub.c */
extern MCDState mcdserver_state;

#ifndef _WIN32
void mcd_sigterm_handler(int signal);
#endif

/**
 * mcdserver_start() - initializes the mcdstub and opens a TCP port
 * @device: TCP port (e.g. tcp::1235)
 */
int mcdserver_start(const char *device);

/**
 * mcd_init_mcdserver_state() - Initializes the mcdserver_state struct.
 *
 * This function allocates memory for the mcdserver_state struct and sets
 * all of its members to their inital values. This includes setting the
 * cpu_state to halted and initializing the query functions with
 * :c:func:`init_query_cmds_table`.
 */
void mcd_init_mcdserver_state(void);

/**
 * init_query_cmds_table() - Initializes all query functions.
 *
 * This function adds all query functions to the mcd_query_cmds_table. This
 * includes their command string, handler function and parameter schema.
 * @mcd_query_cmds_table: Lookup table with all query commands.
 */
void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table);
/**
 * create_processes() - Sorts all processes and calls
 * :c:func:`mcd_create_default_process`.
 *
 * This function sorts all connected processes with the qsort function.
 * Afterwards, it creates a new process with
 * :c:func:`mcd_create_default_process`.
 * @s: A MCDState object.
 */
void create_processes(MCDState *s);

/**
 * mcd_create_default_process() - Creates a default process for debugging.
 *
 * This function creates a new, not yet attached, process with an ID one above
 * the previous maximum ID.
 * @s: A MCDState object.
 */
void mcd_create_default_process(MCDState *s);

/**
 * find_cpu_clusters() - Returns the CPU cluster of the child object.
 *
 * @param[in] child Object with unknown CPU cluster.
 * @param[in] opaque Pointer to an MCDState object.
 */
int find_cpu_clusters(Object *child, void *opaque);

/**
 * pid_order() - Compares process IDs.
 *
 * This function returns -1 if process "a" has a ower process ID than "b".
 * If "b" has a lower ID than "a" 1 is returned and if they are qual 0 is
 * returned.
 * @a: Process a.
 * @b: Process b.
 */
int pid_order(const void *a, const void *b);

/**
 * mcd_chr_can_receive() - Returns the maximum packet length of a TCP packet.
 */
int mcd_chr_can_receive(void *opaque);

/**
 * mcd_chr_receive() - Handles receiving a TCP packet.
 *
 * This function gets called by QEMU when a TCP packet is received.
 * It iterates over that packet an calls :c:func:`mcd_read_byte` for each char
 * of the packet.
 * @buf: Content of the packet.
 * @size: Length of the packet.
 */
void mcd_chr_receive(void *opaque, const uint8_t *buf, int size);

/**
 * mcd_chr_event() - Handles a TCP client connect.
 *
 * This function gets called by QEMU when a TCP cliet connects to the opened
 * TCP port. It attaches the first process. From here on TCP packets can be
 * exchanged.
 * @event: Type of event.
 */
void mcd_chr_event(void *opaque, QEMUChrEvent event);

/**
 * mcd_supports_guest_debug() - Returns true if debugging the selected
 * accelerator is supported.
 */
bool mcd_supports_guest_debug(void);

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
void mcd_vm_state_change(void *opaque, bool running, RunState state);
/**
 * mcd_get_cpu_process() - Returns the process of the provided CPU.
 *
 * @cpu: The CPU state.
 */
MCDProcess *mcd_get_cpu_process(CPUState *cpu);

/**
 * mcd_set_stop_cpu() - Sets c_cpu to the just stopped CPU.
 *
 * @cpu: The CPU state.
 */
void mcd_set_stop_cpu(CPUState *cpu);

/**
 * mcd_get_cpu_pid() - Returns the process ID of the provided CPU.
 *
 * @cpu: The CPU state.
 */
uint32_t mcd_get_cpu_pid(CPUState *cpu);

/**
 * mcd_get_process() - Returns the process of the provided pid.
 *
 * @pid: The process ID.
 */
MCDProcess *mcd_get_process(uint32_t pid);

/**
 * mcd_first_attached_cpu() - Returns the first CPU with an attached process.
 */
CPUState *mcd_first_attached_cpu(void);

/**
 * mcd_next_attached_cpu() - Returns the first CPU with an attached process
 * starting after the
 * provided cpu.
 *
 * @cpu: The CPU to start from.
 */
CPUState *mcd_next_attached_cpu(CPUState *cpu);

/**
 * mcd_get_cpu() - Returns the CPU the index i_cpu_index.
 *
 * @cpu_index: Index of the desired CPU.
 */
CPUState *mcd_get_cpu(uint32_t cpu_index);
/**
 * get_first_cpu_in_process() - Returns the first CPU in the provided process.
 *
 * @process: The process to look in.
 */
CPUState *get_first_cpu_in_process(MCDProcess *process);

/**
 * find_cpu() - Returns the CPU with an index equal to the thread_id.
 *
 * @thread_id: ID of the desired CPU.
 */
CPUState *find_cpu(uint32_t thread_id);
