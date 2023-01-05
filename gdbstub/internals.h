/*
 * gdbstub internals
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GDBSTUB_INTERNALS_H
#define GDBSTUB_INTERNALS_H

#define MAX_PACKET_LENGTH 4096

/*
 * Shared structures and definitions
 */

typedef struct GDBProcess {
    uint32_t pid;
    bool attached;

    char target_xml[1024];
} GDBProcess;

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_GETLINE_ESC,
    RS_GETLINE_RLE,
    RS_CHKSUM1,
    RS_CHKSUM2,
};

/* Temporary home */
#ifdef CONFIG_USER_ONLY
typedef struct {
    int fd;
    char *socket_path;
    int running_state;
} GDBUserState;
#else
typedef struct {
    CharBackend chr;
    Chardev *mon_chr;
} GDBSystemState;
#endif

typedef struct GDBState {
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
#ifdef CONFIG_USER_ONLY
    GDBUserState user;
#else
    GDBSystemState system;
#endif
    bool multiprocess;
    GDBProcess *processes;
    int process_num;
    char syscall_buf[256];
    gdb_syscall_complete_cb current_syscall_cb;
    GString *str_buf;
    GByteArray *mem_buf;
    int sstep_flags;
    int supported_sstep_flags;
} GDBState;


/*
 * Inline utility function, convert from int to hex and back
 */

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

/*
 * Connection helpers for both softmmu and user backends
 */

void gdb_put_strbuf(void);
int gdb_put_packet(const char *buf);
int gdb_put_packet_binary(const char *buf, int len, bool dump);
void gdb_hextomem(GByteArray *mem, const char *buf, int len);
void gdb_memtohex(GString *buf, const uint8_t *mem, int len);
void gdb_memtox(GString *buf, const char *mem, int len);
void gdb_read_byte(uint8_t ch);

/* utility helpers */
CPUState *gdb_first_attached_cpu(void);
void gdb_append_thread_id(CPUState *cpu, GString *buf);
int gdb_get_cpu_index(CPUState *cpu);

void gdb_init_gdbserver_state(void);
void gdb_create_default_process(GDBState *s);

/*
 * Helpers with separate softmmu and user implementations
 */
void gdb_put_buffer(const uint8_t *buf, int len);

/*
 * Break/Watch point support - there is an implementation for softmmu
 * and user mode.
 */
bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, hwaddr addr, hwaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, hwaddr addr, hwaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

#endif /* GDBSTUB_INTERNALS_H */
