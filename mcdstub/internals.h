/*
 * this header includes a lookup table for the transmitted messages over the tcp connection to trace32,
 * as well as function declarations for all functios used inside the mcdstub
 */

#ifndef MCDSTUB_INTERNALS_H
#define MCDSTUB_INTERNALS_H

#include "exec/cpu-common.h"
#include "chardev/char.h"

#define MAX_PACKET_LENGTH 1024

/*
 * lookuptable for transmitted signals
 */

enum {
    MCD_SIGNAL_HANDSHAKE = 0
};


/*
 * struct for an MCD Process, each process can establish one connection
 */

typedef struct MCDProcess {
    //this is probably what we would call a system (in qemu its a cluster)
    uint32_t pid;
    bool attached;

    char target_xml[1024];
} MCDProcess;

typedef void (*MCDCmdHandler)(GArray *params, void *user_ctx);
typedef struct MCDCmdParseEntry {
    MCDCmdHandler handler;
    const char *cmd;
    bool cmd_startswith;
    const char *schema;
} MCDCmdParseEntry;

typedef enum MCDThreadIdKind {
    GDB_ONE_THREAD = 0,
    GDB_ALL_THREADS,     /* One process, all threads */
    GDB_ALL_PROCESSES,
    GDB_READ_THREAD_ERR
} MCDThreadIdKind;

typedef union MCDCmdVariant {
    const char *data;
    uint8_t opcode;
    unsigned long val_ul;
    unsigned long long val_ull;
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
    //RS_GETLINE_ESC,
    //RS_GETLINE_RLE,
    //RS_CHKSUM1,
    //RS_CHKSUM2,
};

typedef struct MCDState {
    bool init;       /* have we been initialised? */
    CPUState *c_cpu; /* current CPU for step/continue ops */
    CPUState *g_cpu; /* current CPU for other ops */
    CPUState *query_cpu; /* for q{f|s}ThreadInfo */
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
} MCDState;

/* lives in main mcdstub.c */
extern MCDState mcdserver_state;


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


/*old functions
void mcd_init_mcdserver_state(void);
int mcd_open_tcp_socket(int tcp_port);
int mcd_extract_tcp_port_num(const char *in_string, char *out_string);
*/
#ifndef _WIN32
void mcd_sigterm_handler(int signal);
#endif

void mcd_init_mcdserver_state(void);
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
void handle_core_open(GArray *params, void *user_ctx);
void handle_query_reset(GArray *params, void *user_ctx);
void handle_detach(GArray *params, void *user_ctx);
void mcd_continue(void);

/* sycall handling */
void mcd_syscall_reset(void);
void mcd_disable_syscalls(void);

#endif /* MCDSTUB_INTERNALS_H */
