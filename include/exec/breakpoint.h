/*
 * QEMU breakpoint & watchpoint definitions
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EXEC_BREAKPOINT_H
#define EXEC_BREAKPOINT_H

#include "qemu/interval-tree.h"
#include "exec/vaddr.h"
#include "exec/memattrs.h"

typedef uint32_t BreakpointFlags;

/* Breakpoint/watchpoint flags */
#define BP_MEM_READ           0x01
#define BP_MEM_WRITE          0x02
#define BP_MEM_ACCESS         (BP_MEM_READ | BP_MEM_WRITE)
#define BP_STOP_BEFORE_ACCESS 0x04
/* 0x08 currently unused */
#define BP_GDB                0x10
#define BP_CPU                0x20
#define BP_ANY                (BP_GDB | BP_CPU)
#define BP_HIT_SHIFT          6
#define BP_WATCHPOINT_HIT_READ  (BP_MEM_READ << BP_HIT_SHIFT)
#define BP_WATCHPOINT_HIT_WRITE (BP_MEM_WRITE << BP_HIT_SHIFT)
#define BP_WATCHPOINT_HIT       (BP_MEM_ACCESS << BP_HIT_SHIFT)

struct CPUBreakpoint {
    IntervalTreeNode itree;     /* start == last == pc */
    BreakpointFlags flags;
    unsigned id;
};

struct CPUWatchpoint {
    IntervalTreeNode itree;
    vaddr hitaddr;
    MemTxAttrs hitattrs;
    BreakpointFlags flags;
    unsigned id;
};

CPUBreakpoint *cpu_breakpoint_insert(CPUState *cpu, vaddr pc,
                                     BreakpointFlags flags, unsigned id);
int cpu_breakpoint_remove(CPUState *cpu, vaddr pc, BreakpointFlags flags);
void cpu_breakpoint_remove_by_ref(CPUState *cpu, CPUBreakpoint *breakpoint);
void cpu_breakpoint_remove_all(CPUState *cpu, BreakpointFlags mask);
bool cpu_breakpoint_test(CPUState *cpu, vaddr pc, BreakpointFlags mask);

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          BreakpointFlags flags, unsigned id,
                          CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, BreakpointFlags flags);
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *cpu, BreakpointFlags mask);

#endif
