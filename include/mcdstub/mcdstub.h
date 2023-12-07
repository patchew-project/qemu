/*
 * Copyright (c) 2023 Nicolas Eder
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef MCDSTUB_H
#define MCDSTUB_H

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
    bool init;
    CPUState *c_cpu;
    enum RSState state;
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum;
    int line_csum;
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

/* lives in mcdstub.c */
extern MCDState mcdserver_state;

typedef struct mcd_reset_st {
    const char *name;
    uint8_t id;
} mcd_reset_st;

/**
 * mcdserver_start() - initializes the mcdstub and opens a TCP port
 * @device: TCP port (e.g. tcp::1235)
 */
int mcdserver_start(const char *device);

#endif /* MCDSTUB_H */
