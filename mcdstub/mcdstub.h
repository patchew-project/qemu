#ifndef MCDSTUB_INTERNALS_H
#define MCDSTUB_INTERNALS_H

#include "exec/cpu-common.h"
#include "chardev/char.h"
#include "hw/core/cpu.h"
/* just used for the register xml files */
#include "exec/gdbstub.h"

#define MAX_PACKET_LENGTH 1024

/* trigger defines */
#define MCD_TRIG_OPT_DATA_IS_CONDITION 0x00000008
#define MCD_TRIG_ACTION_DBG_DEBUG 0x00000001

typedef uint32_t mcd_core_event_et;
/* TODO: replace mcd defines with custom layer */
enum {
    MCD_CORE_EVENT_NONE            = 0x00000000,
    MCD_CORE_EVENT_MEMORY_CHANGE   = 0x00000001,
    MCD_CORE_EVENT_REGISTER_CHANGE = 0x00000002,
    MCD_CORE_EVENT_TRACE_CHANGE    = 0x00000004,
    MCD_CORE_EVENT_TRIGGER_CHANGE  = 0x00000008,
    MCD_CORE_EVENT_STOPPED         = 0x00000010,
    MCD_CORE_EVENT_CHL_PENDING     = 0x00000020,
    MCD_CORE_EVENT_CUSTOM_LO       = 0x00010000,
    MCD_CORE_EVENT_CUSTOM_HI       = 0x40000000,
};

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
#define ARGUMENT_STRING_LENGTH 64

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

typedef struct GDBRegisterState {
    /* needed for the used gdb functions */
    int base_reg;
    int num_regs;
    gdb_get_reg_cb get_reg;
    gdb_set_reg_cb set_reg;
    const char *xml;
    struct GDBRegisterState *next;
} GDBRegisterState;

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

typedef struct mcd_mem_space_st {
    const char *name;
    uint32_t id;
    uint32_t type;
    uint32_t bits_per_mau;
    uint8_t invariance;
    uint32_t endian;
    uint64_t min_addr;
    uint64_t max_addr;
    uint32_t supported_access_options;
    /* internal */
    bool is_secure;
} mcd_mem_space_st;

typedef struct mcd_reg_group_st {
    const char *name;
    uint32_t id;
} mcd_reg_group_st;

typedef struct xml_attrib {
    char argument[ARGUMENT_STRING_LENGTH];
    char value[ARGUMENT_STRING_LENGTH];
} xml_attrib;

typedef struct mcd_reg_st {
    /* xml info */
    char name[ARGUMENT_STRING_LENGTH];
    char group[ARGUMENT_STRING_LENGTH];
    char type[ARGUMENT_STRING_LENGTH];
    uint32_t bitsize;
    uint32_t id; /* id used by the mcd interface */
    uint32_t internal_id; /* id inside reg type */
    uint8_t reg_type;
    /* mcd metadata */
    uint32_t mcd_reg_group_id;
    uint32_t mcd_mem_space_id;
    uint32_t mcd_reg_type;
    uint32_t mcd_hw_thread_id;
    /* data for op-code */
    uint32_t opcode;
} mcd_reg_st;

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

void mcd_init_mcdserver_state(void);
void init_query_cmds_table(MCDCmdParseEntry *mcd_query_cmds_table);
int init_resets(GArray *resets);
int init_trigger(mcd_trigger_into_st *trigger);
void reset_mcdserver_state(void);
void create_processes(MCDState *s);
void mcd_create_default_process(MCDState *s);
int find_cpu_clusters(Object *child, void *opaque);
int pid_order(const void *a, const void *b);
int mcd_chr_can_receive(void *opaque);
void mcd_chr_receive(void *opaque, const uint8_t *buf, int size);
void mcd_chr_event(void *opaque, QEMUChrEvent event);
bool mcd_supports_guest_debug(void);
void mcd_vm_state_change(void *opaque, bool running, RunState state);
int mcd_put_packet(const char *buf);
int mcd_put_packet_binary(const char *buf, int len, bool dump);
bool mcd_got_immediate_ack(void);
void mcd_put_buffer(const uint8_t *buf, int len);
MCDProcess *mcd_get_cpu_process(CPUState *cpu);
uint32_t mcd_get_cpu_pid(CPUState *cpu);
MCDProcess *mcd_get_process(uint32_t pid);
CPUState *mcd_first_attached_cpu(void);
CPUState *mcd_next_attached_cpu(CPUState *cpu);
void mcd_read_byte(uint8_t ch);
int mcd_handle_packet(const char *line_buf);
void mcd_put_strbuf(void);
void mcd_exit(int code);
void run_cmd_parser(const char *data, const MCDCmdParseEntry *cmd);
int process_string_cmd(void *user_ctx, const char *data,
    const MCDCmdParseEntry *cmds, int num_cmds);
int cmd_parse_params(const char *data, const char *schema, GArray *params);
void handle_vm_start(GArray *params, void *user_ctx);
void handle_vm_step(GArray *params, void *user_ctx);
void handle_vm_stop(GArray *params, void *user_ctx);
void handle_gen_query(GArray *params, void *user_ctx);
int mcd_get_cpu_index(CPUState *cpu);
CPUState *mcd_get_cpu(uint32_t i_cpu_index);
void handle_query_cores(GArray *params, void *user_ctx);
void handle_query_system(GArray *params, void *user_ctx);
CPUState *get_first_cpu_in_process(MCDProcess *process);
CPUState *find_cpu(uint32_t thread_id);
void handle_open_core(GArray *params, void *user_ctx);
void handle_query_reset_f(GArray *params, void *user_ctx);
void handle_query_reset_c(GArray *params, void *user_ctx);
void handle_close_server(GArray *params, void *user_ctx);
void handle_close_core(GArray *params, void *user_ctx);
void handle_query_trigger(GArray *params, void *user_ctx);
void mcd_vm_start(void);
void mcd_cpu_start(CPUState *cpu);
int mcd_cpu_sstep(CPUState *cpu);
void mcd_vm_stop(void);
void handle_query_reg_groups_f(GArray *params, void *user_ctx);
void handle_query_reg_groups_c(GArray *params, void *user_ctx);
void handle_query_mem_spaces_f(GArray *params, void *user_ctx);
void handle_query_mem_spaces_c(GArray *params, void *user_ctx);
void handle_query_regs_f(GArray *params, void *user_ctx);
void handle_query_regs_c(GArray *params, void *user_ctx);
void handle_open_server(GArray *params, void *user_ctx);
void parse_reg_xml(const char *xml, int size, GArray* registers,
    uint8_t reg_type);
void handle_reset(GArray *params, void *user_ctx);
void handle_query_state(GArray *params, void *user_ctx);
void handle_read_register(GArray *params, void *user_ctx);
void handle_write_register(GArray *params, void *user_ctx);
void handle_read_memory(GArray *params, void *user_ctx);
void handle_write_memory(GArray *params, void *user_ctx);
int mcd_read_register(CPUState *cpu, GByteArray *buf, int reg);
int mcd_write_register(CPUState *cpu, GByteArray *buf, int reg);
int mcd_read_memory(CPUState *cpu, hwaddr addr, uint8_t *buf, int len);
int mcd_write_memory(CPUState *cpu, hwaddr addr, uint8_t *buf, int len);
void handle_breakpoint_insert(GArray *params, void *user_ctx);
void handle_breakpoint_remove(GArray *params, void *user_ctx);
int mcd_breakpoint_insert(CPUState *cpu, int type, vaddr addr);
int mcd_breakpoint_remove(CPUState *cpu, int type, vaddr addr);

/* arm specific functions */
int mcd_arm_store_mem_spaces(CPUState *cpu, GArray *memspaces);
int mcd_arm_parse_core_xml_file(CPUClass *cc, GArray *reggroups,
    GArray *registers, int *current_group_id);
int mcd_arm_parse_general_xml_files(CPUState *cpu, GArray* reggroups,
    GArray *registers, int *current_group_id);
int mcd_arm_get_additional_register_info(GArray *reggroups, GArray *registers,
    CPUState *cpu);
/* sycall handling */
void mcd_syscall_reset(void);
void mcd_disable_syscalls(void);

/* helpers */
int int_cmp(gconstpointer a, gconstpointer b);
void mcd_memtohex(GString *buf, const uint8_t *mem, int len);
void mcd_hextomem(GByteArray *mem, const char *buf, int len);
uint64_t atouint64_t(const char *in);
uint32_t atouint32_t(const char *in);

#endif /* MCDSTUB_INTERNALS_H */
