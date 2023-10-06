/*
 * this header includes a lookup table for the transmitted messages over the tcp connection to trace32,
 * as well as function declarations for all functios used inside the mcdstub
 */

#ifndef MCDSTUB_INTERNALS_H
#define MCDSTUB_INTERNALS_H

#include "exec/cpu-common.h"
#include "chardev/char.h"
#include "hw/core/cpu.h"
// just used for the register xml files
#include "exec/gdbstub.h"       /* xml_builtin */

#define MAX_PACKET_LENGTH 1024

// trigger defines
#define MCD_TRIG_TYPE_IP 0x00000001
#define MCD_TRIG_TYPE_READ 0x00000002
#define MCD_TRIG_TYPE_WRITE 0x00000004
#define MCD_TRIG_TYPE_RW 0x00000008
#define MCD_TRIG_OPT_DATA_IS_CONDITION 0x00000008
#define MCD_TRIG_ACTION_DBG_DEBUG 0x00000001

// schema defines
#define ARG_SCHEMA_QRYHANDLE 'q'
#define ARG_SCHEMA_STRING 's'
#define ARG_SCHEMA_INT 'd'
#define ARG_SCHEMA_CORENUM 'c'

// resets
#define RESET_SYSTEM "full_system_reset"
#define RESET_GPR "gpr_reset"
#define RESET_MEMORY "memory_reset"

// more
#define QUERY_TOTAL_NUMBER 11 //FIXME: set this to a usefull value in the end
#define CMD_SCHEMA_LENGTH 3
#define MAX_SCHEMA_ARGS CMD_SCHEMA_LENGTH-1
#define MCD_SYSTEM_NAME "qemu-system"
// tcp query packet values templates
#define DEVICE_NAME_TEMPLATE(s) "qemu-" #s "-device"

// GDB stuff thats needed for GDB function, which we use
typedef struct GDBRegisterState {
    int base_reg;
    int num_regs;
    gdb_get_reg_cb get_reg;
    gdb_set_reg_cb set_reg;
    const char *xml;
    struct GDBRegisterState *next;
} GDBRegisterState;

/*
 * struct for an MCD Process, each process can establish one connection
 */

typedef struct MCDProcess {
    //this is a relict from the gdb process, we might be able to delete this
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

typedef enum MCDThreadIdKind {
    GDB_ONE_THREAD = 0,
    GDB_ALL_THREADS,     /* One process, all threads */
    GDB_ALL_PROCESSES,
    GDB_READ_THREAD_ERR
} MCDThreadIdKind;

typedef union MCDCmdVariant {
    
    const char *data;
    int data_int;
    int query_handle;
    int cpu_id;
    struct {
        MCDThreadIdKind kind;
        uint32_t pid;
        uint32_t tid;
    } thread_id;

} MCDCmdVariant;

#define get_param(p, i)    (&g_array_index(p, MCDCmdVariant, i))


/*
 * not sure for what this is used exactly
 */


enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_DATAEND,
};

typedef struct mcd_trigger_st {
    uint32_t type;
    uint32_t option;
    uint32_t action;
    uint32_t nr_trigger;
} mcd_trigger_st;

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
    //the next one is about stub compatibility and we should be able to assume this is true anyway
    //bool multiprocess;
    MCDProcess *processes;
    int process_num;
    GString *str_buf;
    GByteArray *mem_buf;
    // maybe we don't need those flags
    int sstep_flags;
    int supported_sstep_flags;

    // my stuff
    uint32_t query_cpu_id;
    GList *all_memspaces;
    GList *all_reggroups;
    GList *all_registers;
    GArray *resets;
    mcd_trigger_st trigger;
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
} mcd_mem_space_st;

typedef struct mcd_reg_group_st {
    const char *name;
    uint32_t id;
} mcd_reg_group_st;

typedef struct xml_attrib {
    char argument[64];
    char value[64];
} xml_attrib;

typedef struct mcd_reg_st {
    // xml info
    char name[64];
    char group[64];
    char type[64];
    uint32_t bitsize;
    uint32_t id;
    // mcd metadata
    uint32_t mcd_reg_group_id;
    uint32_t mcd_mem_space_id;
    uint32_t mcd_reg_type;
    uint32_t mcd_hw_thread_id;
    // data for op-code
    uint8_t cp;
    uint8_t crn;
    uint8_t crm;
    uint8_t opc0; // <- might not be needed!
    uint8_t opc1;
    uint8_t opc2;
} mcd_reg_st;

typedef struct mcd_reset_st {
    const char *name;
    uint8_t id;
} mcd_reset_st;

// Inline utility function, convert from int to hex and back


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
void init_query_cmds_table(MCDCmdParseEntry* mcd_query_cmds_table);
int init_resets(GArray* resets);
int init_trigger(mcd_trigger_st* trigger);
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
void mcd_set_stop_cpu(CPUState *cpu);
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
int process_string_cmd(void *user_ctx, const char *data, const MCDCmdParseEntry *cmds, int num_cmds);
int cmd_parse_params(const char *data, const char *schema, GArray *params);
void handle_continue(GArray *params, void *user_ctx);
void handle_gen_query(GArray *params, void *user_ctx);
void mcd_append_thread_id(CPUState *cpu, GString *buf);
int mcd_get_cpu_index(CPUState *cpu);
CPUState* mcd_get_cpu(uint32_t i_cpu_index);
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
void mcd_continue(void);
void handle_query_reg_groups_f(GArray *params, void *user_ctx);
void handle_query_reg_groups_c(GArray *params, void *user_ctx);
void handle_query_mem_spaces_f(GArray *params, void *user_ctx);
void handle_query_mem_spaces_c(GArray *params, void *user_ctx);
void handle_query_regs_f(GArray *params, void *user_ctx);
void handle_query_regs_c(GArray *params, void *user_ctx);
void handle_open_server(GArray *params, void *user_ctx);
void parse_reg_xml(const char *xml, int size, GArray* registers);
void handle_reset(GArray *params, void *user_ctx);

// arm specific functions
int mcd_arm_store_mem_spaces(CPUState *cpu, GArray* memspaces);
int mcd_arm_parse_core_xml_file(CPUClass *cc, GArray* reggroups, GArray* registers, int* current_group_id);
int mcd_arm_parse_general_xml_files(CPUState *cpu, GArray* reggroups, GArray* registers, int* current_group_id);
int mcd_arm_get_additional_register_info(GArray* reggroups, GArray* registers);

/* sycall handling */
void mcd_syscall_reset(void);
void mcd_disable_syscalls(void);

// helpers
int int_cmp(gconstpointer a, gconstpointer b);

#endif /* MCDSTUB_INTERNALS_H */
