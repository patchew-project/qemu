#ifndef MCDSTUB_INTERNALS_H
#define MCDSTUB_INTERNALS_H

#include "exec/cpu-common.h"
#include "chardev/char.h"
#include "hw/core/cpu.h"
#include "mcdstub_common.h"

#define MAX_PACKET_LENGTH 1024

/* trigger defines */
#define MCD_TRIG_OPT_DATA_IS_CONDITION 0x00000008
#define MCD_TRIG_ACTION_DBG_DEBUG 0x00000001

/* schema defines */
#define ARG_SCHEMA_QRYHANDLE 'q'
#define ARG_SCHEMA_STRING 's'
#define ARG_SCHEMA_INT 'd'
#define ARG_SCHEMA_UINT64_T 'l'
#define ARG_SCHEMA_CORENUM 'c'
#define ARG_SCHEMA_HEXDATA 'h'

/* resets */
#define RESET_SYSTEM "full_system_reset"
#define RESET_GPR "gpr_reset"
#define RESET_MEMORY "memory_reset"

/* misc */
#define QUERY_TOTAL_NUMBER 12
#define CMD_SCHEMA_LENGTH 6
#define MCD_SYSTEM_NAME "qemu-system"

/* supported architectures */
#define MCDSTUB_ARCH_ARM "arm"

/* tcp query packet values templates */
#define DEVICE_NAME_TEMPLATE(s) "qemu-" #s "-device"

/* state strings */
#define STATE_STR_UNKNOWN(d) "cpu " #d " in unknown state"
#define STATE_STR_DEBUG(d) "cpu " #d " in debug state"
#define STATE_STR_RUNNING(d) "cpu " #d " running"
#define STATE_STR_HALTED(d) "cpu " #d " currently halted"
#define STATE_STR_INIT_HALTED "vm halted since boot"
#define STATE_STR_INIT_RUNNING "vm running since boot"
#define STATE_STR_BREAK_HW "stopped beacuse of HW breakpoint"
#define STATE_STEP_PERFORMED "stopped beacuse of single step"
#define STATE_STR_BREAK_READ(d) "stopped beacuse of read access at " #d
#define STATE_STR_BREAK_WRITE(d) "stopped beacuse of write access at " #d
#define STATE_STR_BREAK_RW(d) "stopped beacuse of read or write access at " #d
#define STATE_STR_BREAK_UNKNOWN "stopped for unknown reason"

typedef struct MCDProcess {
    uint32_t pid;
    bool attached;

    char target_xml[1024];
} MCDProcess;

typedef void (*MCDCmdHandler)(GArray *params, void *user_ctx);
typedef struct MCDCmdParseEntry {
    MCDCmdHandler handler;
    const char *cmd;
    char schema[CMD_SCHEMA_LENGTH];
} MCDCmdParseEntry;

typedef union MCDCmdVariant {
    const char *data;
    uint32_t data_uint32_t;
    uint64_t data_uint64_t;
    uint32_t query_handle;
    uint32_t cpu_id;
} MCDCmdVariant;

#define get_param(p, i)    (&g_array_index(p, MCDCmdVariant, i))

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_DATAEND,
};

typedef struct breakpoint_st {
    uint32_t type;
    uint64_t address;
    uint32_t id;
} breakpoint_st;

typedef struct mcd_trigger_into_st {
    char type[ARGUMENT_STRING_LENGTH];
    char option[ARGUMENT_STRING_LENGTH];
    char action[ARGUMENT_STRING_LENGTH];
    uint32_t nr_trigger;
} mcd_trigger_into_st;

typedef struct mcd_cpu_state_st {
    const char *state;
    bool memory_changed;
    bool registers_changed;
    bool target_was_stopped;
    uint32_t bp_type;
    uint64_t bp_address;
    const char *stop_str;
    const char *info_str;
} mcd_cpu_state_st;

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

typedef struct xml_attrib {
    char argument[ARGUMENT_STRING_LENGTH];
    char value[ARGUMENT_STRING_LENGTH];
} xml_attrib;

typedef struct mcd_reset_st {
    const char *name;
    uint8_t id;
} mcd_reset_st;

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9') {
        return v - '0';
    } else if (v >= 'A' && v <= 'F') {
        return v - 'A' + 10;
    } else if (v >= 'a' && v <= 'f') {
        return v - 'a' + 10;
    } else {
        return 0;
    }
}

static inline int tohex(int v)
{
    if (v < 10) {
        return v + '0';
    } else {
        return v - 10 + 'a';
    }
}

#ifndef _WIN32
void mcd_sigterm_handler(int signal);
#endif

/**
 * \defgroup mcdstub Main mcdstub functions
 * All architecture independent mcdstub functions.
 */

/**
 * \addtogroup mcdstub
 * @{
 */

/**
 * \brief Initializes the mcdserver_state struct.
 *
 * This function allocates memory for the mcdserver_state struct and sets
 * all of its members to their inital values. This includes setting the
 * cpu_state to halted and initializing the query functions with \ref
 * init_query_cmds_table.
 */
void mcd_init_mcdserver_state(void);
/**
 * \brief Initializes all query functions.
 *
 * This function adds all query functions to the mcd_query_cmds_table. This
 * includes their command string, handler function and parameter schema.
 * @param[out] mcd_query_cmds_table Lookup table with all query commands.
 */
void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table);
/**
 * \brief Initializes the resets info.
 *
 * This function currently only adds all theoretical possible resets to the
 * resets GArray. None of the resets work at the moment. The resets are:
 * "full_system_reset", "gpr_reset" and "memory_reset".
 * @param[out] resets GArray with possible resets.
 */
int init_resets(GArray *resets);
/**
 * \brief Initializes the trigger info.
 *
 * This function adds the types of trigger, their possible options and actions
 * to the trigger struct.
 * @param[out] trigger Struct with all trigger info.
 */
int init_trigger(mcd_trigger_into_st *trigger);
/**
 * \brief Resets the mcdserver_state struct.
 *
 * This function deletes all processes connected to the mcdserver_state.
 */
void reset_mcdserver_state(void);
/**
 * \brief Sorts all processes and calls \ref mcd_create_default_process.
 *
 * This function sorts all connected processes with the qsort function.
 * Afterwards, it creates a new process with \ref mcd_create_default_process.
 * @param[in] s A MCDState object.
 */
void create_processes(MCDState *s);
/**
 * \brief Creates a default process for debugging.
 *
 * This function creates a new, not yet attached, process with an ID one above
 * the previous maximum ID.
 * @param[in] s A MCDState object.
 */
void mcd_create_default_process(MCDState *s);
/**
 * \brief Returns the CPU cluster of the child object.
 *
 * @param[in] child Object with unknown CPU cluster.
 * @param[in] opaque Pointer to an MCDState object.
 */
int find_cpu_clusters(Object *child, void *opaque);
/**
 * \brief Compares process IDs.
 *
 * This function returns -1 if process "a" has a ower process ID than "b".
 * If "b" has a lower ID than "a" 1 is returned and if they are qual 0 is
 * returned. 
 * @param[in] a Process a.
 * @param[in] b Process b.
 */
int pid_order(const void *a, const void *b);
/**
 * \brief Returns the maximum packet length of a TCP packet.
 */
int mcd_chr_can_receive(void *opaque);
/**
 * \brief Handles receiving a TCP packet.
 * 
 * This function gets called by QEMU when a TCP packet is received.
 * It iterates over that packet an calls \ref mcd_read_byte for each char
 * of the packet.
 * @param[in] buf Content of the packet.
 * @param[in] size Length of the packet.
 */
void mcd_chr_receive(void *opaque, const uint8_t *buf, int size);
/**
 * \brief Handles a TCP client connect.
 * 
 * This function gets called by QEMU when a TCP cliet connects to the opened
 * TCP port. It attaches the first process. From here on TCP packets can be
 * exchanged.
 * @param[in] event Type of event.
 */
void mcd_chr_event(void *opaque, QEMUChrEvent event);
/**
 * \brief Returns true if debugging the selected accelerator is supported.
 */
bool mcd_supports_guest_debug(void);
/**
 * \brief Handles a state change of the QEMU VM.
 * 
 * This function is called when the QEMU VM goes through a state transition.
 * It stores the runstate the CPU is in to the cpu_state and when in
 * \c RUN_STATE_DEBUG it collects additional data on what watchpoint was hit.
 * This function also resets the singlestep behavior.
 * @param[in] running True if he VM is running.
 * @param[in] state The new (and active) VM run state.
 */
void mcd_vm_state_change(void *opaque, bool running, RunState state);
/**
 * \brief Calls \ref mcd_put_packet_binary with buf and length of buf.
 * 
 * @param[in] buf TCP packet data.
 */
int mcd_put_packet(const char *buf);
/**
 * \brief Adds footer and header to the TCP packet data in buf.
 * 
 * Besides adding header and footer, this function also stores the complete TCP
 * packet in the last_packet member of the mcdserver_state. Then the packet
 * gets send with the \ref mcd_put_buffer function.
 * @param[in] buf TCP packet data.
 * @param[in] len TCP packet length.
 */
int mcd_put_packet_binary(const char *buf, int len);
/**
 * \brief Always returns true, currently needed for copatibility.
 */
bool mcd_got_immediate_ack(void);
/**
 * \brief Sends the buf as TCP packet with qemu_chr_fe_write_all.
 * 
 * @param[in] buf TCP packet data.
 * @param[in] len TCP packet length.
 */
void mcd_put_buffer(const uint8_t *buf, int len);
/**
 * \brief Returns the process of the provided CPU.
 * 
 * @param[in] cpu The CPU state.
 */
MCDProcess *mcd_get_cpu_process(CPUState *cpu);
/**
 * \brief Returns the process ID of the provided CPU.
 * 
 * @param[in] cpu The CPU state.
 */
uint32_t mcd_get_cpu_pid(CPUState *cpu);
/**
 * \brief Returns the process of the provided pid.
 * 
 * @param[in] pid The process ID.
 */
MCDProcess *mcd_get_process(uint32_t pid);
/**
 * \brief Returns the first CPU with an attached process.
 */
CPUState *mcd_first_attached_cpu(void);
/**
 * \brief Returns the first CPU with an attached process starting after the 
 * provided cpu.
 * 
 * @param[in] cpu The CPU to start from.
 */
CPUState *mcd_next_attached_cpu(CPUState *cpu);
/**
 * \brief Resends the last packet if not acknowledged and extracts the data
 * from a received TCP packet.
 * 
 * In case the last sent packet was not acknowledged from the mcdstub,
 * this function resends it.
 * If it was acknowledged this function parses the incoming packet
 * byte by byte.
 * It extracts the data in the packet and sends an
 * acknowledging response when finished. Then \ref mcd_handle_packet gets
 * called.
 * @param[in] ch Character of the received TCP packet, which should be parsed.
 */
void mcd_read_byte(uint8_t ch);
/**
 * \brief Evaluates the type of received packet and chooses the correct handler.
 * 
 * This function takes the first character of the line_buf to determine the
 * type of packet. Then it selects the correct handler function and parameter
 * schema. With this info it calls \ref run_cmd_parser.
 * @param[in] line_buf TCP packet data.
 */
int mcd_handle_packet(const char *line_buf);
/**
 * \brief Calls \ref mcd_put_packet with the str_buf of the mcdserver_state.
 */
void mcd_put_strbuf(void);
/**
 * \brief Terminates QEMU.
 * 
 * If the mcdserver_state has not been initialized the function exits before
 * terminating QEMU. Terminting is done with the qemu_chr_fe_deinit function.
 * @param[in] code An exitcode, which can be used in the future.
 */
void mcd_exit(int code);
/**
 * \brief Prepares the mcdserver_state before executing TCP packet functions.
 * 
 * This function empties the str_buf and mem_buf of the mcdserver_state and
 * then calls \ref process_string_cmd. In case this function fails, an empty
 * TCP packet is sent back the MCD Shared Library.
 * @param[in] data TCP packet data.
 * @param[in] cmd Handler function (can be an array of functions).
 */
void run_cmd_parser(const char *data, const MCDCmdParseEntry *cmd);
/**
 * \brief Collects all parameters from the data and calls the correct handler.
 * 
 * The parameters are extracted with the \ref cmd_parse_params function.
 * This function selects the command in the cmds array, which fits the start of
 * the data string. This way the correct commands is selected.
 * @param[in] data TCP packet data.
 * @param[in] cmds Array of possible commands.
 * @param[in] num_cmds Number of commands in the cmds array.
 */
int process_string_cmd(void *user_ctx, const char *data,
    const MCDCmdParseEntry *cmds, int num_cmds);
/**
 * \brief Extracts all parameters from a TCP packet.
 * 
 * This function uses the schema parameter to determine which type of parameter
 * to expect. It then extracts that parameter from the data and stores it in
 * the params GArray.
 * @param[in] data TCP packet data.
 * @param[in] schema List of expected parameters for the packet.
 * @param[out] params GArray with all extracted parameters.
 */
int cmd_parse_params(const char *data, const char *schema, GArray *params);
/**
 * \brief Handler for the VM start TCP packet.
 * 
 * Evaluates whether all cores or just a perticular core should get started and
 * calls \ref mcd_vm_start or \ref mcd_cpu_start respectively.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_vm_start(GArray *params, void *user_ctx);
/**
 * \brief Handler for the VM step TCP packet.
 * 
 * Calls \ref mcd_cpu_sstep for the CPU which sould be stepped. Stepping all
 * CPUs is currently not supported.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_vm_step(GArray *params, void *user_ctx);
/**
 * \brief Handler for the VM stop TCP packet.
 * 
 * Always calls \ref mcd_vm_stop and stops all cores. Stopping individual cores
 * is currently not supported
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_vm_stop(GArray *params, void *user_ctx);
/**
 * \brief Handler for all TCP query packets.
 * 
 * Calls \ref process_string_cmd with all query functions in the
 * mcd_query_cmds_table. \ref process_string_cmd then selects the correct one.
 * This function just passes on the TCP packet data string from the parameters.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_gen_query(GArray *params, void *user_ctx);
/**
 * \brief Returns the internal CPU index plus one.
 * 
 * @param[in] cpu CPU of interest.
 */
int mcd_get_cpu_index(CPUState *cpu);
/**
 * \brief Returns the CPU the index i_cpu_index.
 * 
 * @param[in] cpu_index Index of the desired CPU.
 */
CPUState *mcd_get_cpu(uint32_t cpu_index);
/**
 * \brief Handler for the core query.
 * 
 * This function sends the type of core and number of cores currently
 * simulated by QEMU. It also sends a device name for the MCD data structure.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_cores(GArray *params, void *user_ctx);
/**
 * \brief Handler for the system query.
 * 
 * Sends the system name, which is "qemu-system".
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_system(GArray *params, void *user_ctx);
/**
 * \brief Returns the first CPU in the provided process.
 * 
 * @param[in] process The process to look in.
 */
CPUState *get_first_cpu_in_process(MCDProcess *process);
/**
 * \brief Returns the CPU with an index equal to the thread_id.
 * 
 * @param[in] thread_id ID of the desired CPU.
 */
CPUState *find_cpu(uint32_t thread_id);
/**
 * \brief Handler for opening a core.
 * 
 * This function initializes all data for the core with the ID provided in
 * the first parameter. In has a swtich case for different architectures.
 * Currently only 32-Bit ARM is supported. The data includes memory spaces,
 * register groups and registers themselves. They get stored into GLists where
 * every entry in the list corresponds to one opened core.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_open_core(GArray *params, void *user_ctx);
/**
 * \brief Handler for the first reset query.
 * 
 * This function sends the first reset name and ID.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_reset_f(GArray *params, void *user_ctx);
/**
 * \brief Handler for all consecutive reset queries.
 * 
 * This functions sends all consecutive reset names and IDs. It uses the
 * query_index parameter to determine which reset is queried next.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_reset_c(GArray *params, void *user_ctx);
/**
 * \brief Handler for closing the MCD server.
 * 
 * This function detaches the debugger (process) and frees up memory.
 * Then it start the QEMU VM with \ref mcd_vm_start.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_close_server(GArray *params, void *user_ctx);
/**
 * \brief Handler for closing a core.
 * 
 * Frees all memory allocated for core specific information. This includes
 * memory spaces, register groups and registers.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_close_core(GArray *params, void *user_ctx);
/**
 * \brief Handler for trigger query.
 * 
 * Sends data on the different types of trigger and their options and actions.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_trigger(GArray *params, void *user_ctx);
/**
 * \brief Starts all CPUs with the vm_start function.
 */
void mcd_vm_start(void);
/**
 * \brief Starts the selected CPU with the cpu_resume function.
 * 
 * @param[in] cpu The CPU about to be started.
 */
void mcd_cpu_start(CPUState *cpu);
/**
 * \brief Performes a step on the selected CPU.
 * 
 * This function first sets the correct single step flags for the CPU with
 * cpu_single_step and then starts the CPU with cpu_resume.
 * @param[in] cpu The CPU about to be stepped.
 */
int mcd_cpu_sstep(CPUState *cpu);
/**
 * \brief Brings all CPUs in debug state with the vm_stop function.
 */
void mcd_vm_stop(void);
/**
 * \brief Handler for the first register group query.
 * 
 * This function sends the first register group name and ID.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_reg_groups_f(GArray *params, void *user_ctx);
/**
 * \brief Handler for all consecutive register group queries.
 * 
 * This function sends all consecutive register group names and IDs. It uses
 * the query_index parameter to determine which register group is queried next.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_reg_groups_c(GArray *params, void *user_ctx);
/**
 * \brief Handler for the first memory space query.
 * 
 * This function sends the first memory space name, ID, type and accessing
 * options.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_mem_spaces_f(GArray *params, void *user_ctx);
/**
 * \brief Handler for all consecutive memory space queries.
 * 
 * This function sends all consecutive memory space names, IDs, types and
 * accessing options.
 * It uses the query_index parameter to determine
 * which memory space is queried next.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_mem_spaces_c(GArray *params, void *user_ctx);
/**
 * \brief Handler for the first register query.
 * 
 * This function sends the first register with all its information.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_regs_f(GArray *params, void *user_ctx);
/**
 * \brief Handler for all consecutive register queries.
 * 
 * This function sends all consecutive registers with all their information.
 * It uses the query_index parameter to determine
 * which register is queried next.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_regs_c(GArray *params, void *user_ctx);
/**
 * \brief Handler for opening the MCD server.
 * 
 * This is the first function that gets called from the MCD Shared Library.
 * It initializes core indepent data with the \ref init_resets and
 * \reg init_trigger functions. It also send the \c TCP_HANDSHAKE_SUCCESS
 * packet back to the library to confirm the mcdstub is ready for further
 * communication.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_open_server(GArray *params, void *user_ctx);
/**
 * \brief Handler for performing resets.
 * 
 * This function is currently not in use.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_reset(GArray *params, void *user_ctx);
/**
 * \brief Handler for the state query.
 * 
 * This function collects all data stored in the
 * cpu_state member of the mcdserver_state and formats and sends it to the
 * library.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_query_state(GArray *params, void *user_ctx);
/**
 * \brief Handler for reading a register.
 * 
 * This function calls \ref mcd_read_register to read a register. The register
 * data gets stored in the mem_buf byte array. The data then gets converted
 * into a hex string with \ref mcd_memtohex and then send.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_read_register(GArray *params, void *user_ctx);
/**
 * \brief Handler for writing a register.
 * 
 * This function converts the incoming hex string data into a byte array with
 * \ref mcd_hextomem. Then it calls \ref mcd_write_register to write to the
 * register.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_write_register(GArray *params, void *user_ctx);
/**
 * \brief Handler for reading memory.
 * 
 * First, this function checks whether reading a secure memory space is
 * requested and changes the access mode with \ref arm_mcd_set_scr.
 * Then it calls \ref mcd_read_memory to read memory. The collected
 * data gets stored in the mem_buf byte array. The data then gets converted
 * into a hex string with \ref mcd_memtohex and then send.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_read_memory(GArray *params, void *user_ctx);
/**
 * \brief Handler for writing memory.
 * 
 * First, this function checks whether reading a secure memory space is
 * requested and changes the access mode with \ref arm_mcd_set_scr.
 * Then it converts the incoming hex string data into a byte array with
 * \ref mcd_hextomem. Then it calls \ref mcd_write_memory to write to the
 * register.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_write_memory(GArray *params, void *user_ctx);
/**
 * \brief Reads a registers data and stores it into the buf.
 * 
 * This function collects the register type and internal ID
 * (depending on the XML file). Then it calls the architecture specific
 * read function. For ARM this is \ref arm_mcd_read_register.
 * @param[in] cpu CPU to which the register belongs.
 * @param[out] buf Byte array with register data.
 * @param[in] reg General ID of the register.
 */
int mcd_read_register(CPUState *cpu, GByteArray *buf, int reg);
/**
 * \brief Writes data from the buf to a register.
 * 
 * This function collects the register type and internal ID
 * (depending on the XML file). Then it calls the architecture specific
 * write function. For ARM this is \ref arm_mcd_write_register.
 * @param[in] cpu CPU to which the register belongs.
 * @param[in] buf Byte array with register data.
 * @param[in] reg General ID of the register.
 */
int mcd_write_register(CPUState *cpu, GByteArray *buf, int reg);
/**
 * \brief Reads memory data and stores it into the buf.
 * 
 * This function calls cpu_memory_rw_debug or if available
 * memory_rw_debug in read mode and passes on all parameters.
 * @param[in] cpu CPU to which the memory belongs.
 * @param[in] addr Address to start reading.
 * @param[out] buf Byte array with memory data.
 * @param[in] len Number of bytes to be read.
 */
int mcd_read_memory(CPUState *cpu, hwaddr addr, uint8_t *buf, int len);
/**
 * \brief Writes data from the buf into the memory.
 * 
 * This function calls cpu_memory_rw_debug or if available
 * memory_rw_debug in write mode and passes on all parameters.
 * @param[in] cpu CPU to which the memory belongs.
 * @param[in] addr Address to start writing.
 * @param[in] buf Byte array with memory data.
 * @param[in] len Number of bytes to be written.
 */
int mcd_write_memory(CPUState *cpu, hwaddr addr, uint8_t *buf, int len);
/**
 * \brief Handler for inserting a break- or watchpoint.
 * 
 * This function extracts the CPU, breakpoint type and address from the
 * parameters and calls \ref mcd_breakpoint_insert to insert the breakpoint.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_breakpoint_insert(GArray *params, void *user_ctx);
/**
 * \brief Handler for inserting a break- or watchpoint.
 * 
 * This function extracts the CPU, breakpoint type and address from the
 * parameters and calls \ref mcd_breakpoint_remove to insert the breakpoint.
 * @param[in] params GArray with all TCP packet parameters.
 */
void handle_breakpoint_remove(GArray *params, void *user_ctx);
/**
 * \brief Inserts a break- or watchpoint.
 * 
 * This function evaluates the received breakpoint type and translates it
 * to a known GDB breakpoint type.
 * Then it calls cpu_breakpoint_insert or cpu_watchpoint_insert depending on
 * the type.
 * @param[in] cpu CPU to which the breakpoint should be added.
 * @param[in] addr Address of the breakpoint.
 * @param[in] type Breakpoint type.
 */
int mcd_breakpoint_insert(CPUState *cpu, int type, vaddr addr);
/**
 * \brief Removes a break- or watchpoint.
 * 
 * This function evaluates the received breakpoint type and translates it
 * to a known GDB breakpoint type.
 * Then it calls cpu_breakpoint_remove or cpu_watchpoint_remove depending on
 * the type.
 * @param[in] cpu CPU from which the breakpoint should be removed.
 * @param[in] addr Address of the breakpoint.
 * @param[in] type Breakpoint type.
 */
int mcd_breakpoint_remove(CPUState *cpu, int type, vaddr addr);

/** @} */

/* sycall handling */
void mcd_syscall_reset(void);
void mcd_disable_syscalls(void);

/* helpers */

/**
 * \defgroup helperfunctions Helper mcdstub functions
 * Supporting functions used throughout the mcdstub.
 */

/**
 * \addtogroup helperfunctions
 * @{
 */

/**
 * \brief Compares a and b and returns zero if they are equal.
 *
 * @param[in] a Pointer to integer a.
 * @param[in] b Pointer to integer b.
 */
int int_cmp(gconstpointer a, gconstpointer b);
/**
 * \brief Converts a byte array into a hex string.
 *
 * @param[in] mem Pointer to byte array.
 * @param[out] buf Pointer to hex string.
 * @param[in] len Number of bytes.
 */
void mcd_memtohex(GString *buf, const uint8_t *mem, int len);
/**
 * \brief Converts a hex string into a byte array.
 *
 * @param[out] mem Pointer to byte array.
 * @param[in] buf Pointer to hex string.
 * @param[in] len Number of bytes.
 */
void mcd_hextomem(GByteArray *mem, const char *buf, int len);
/**
 * \brief Converts a string into a unsigned 64 bit integer.
 *
 * @param[in] in Pointer to input string.
 */
uint64_t atouint64_t(const char *in);
/**
 * \brief Converts a string into a unsigned 32 bit integer.
 *
 * @param[in] in Pointer to input string.
 */
uint32_t atouint32_t(const char *in);

/** @} */

#endif /* MCDSTUB_INTERNALS_H */
