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
 * Break/Watch point support - there is an implementation for softmmu
 * and user mode.
 */
bool gdb_supports_guest_debug(void);
int gdb_breakpoint_insert(CPUState *cs, int type, hwaddr addr, hwaddr len);
int gdb_breakpoint_remove(CPUState *cs, int type, hwaddr addr, hwaddr len);
void gdb_breakpoint_remove_all(CPUState *cs);

#endif /* GDBSTUB_INTERNALS_H */
