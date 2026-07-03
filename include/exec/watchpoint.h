/*
 * CPU watchpoints
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef EXEC_WATCHPOINT_H
#define EXEC_WATCHPOINT_H

#include "qemu/queue.h"
#include "exec/breakpoint.h"
#include "exec/memattrs.h"
#include "exec/vaddr.h"

typedef struct CPUWatchpoint {
    vaddr vaddr;
    vaddr len;
    vaddr hitaddr;
    MemTxAttrs hitattrs;
    BreakpointFlags flags;
    QTAILQ_ENTRY(CPUWatchpoint) entry;
} CPUWatchpoint;

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          BreakpointFlags flags, CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, BreakpointFlags flags);
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *cpu, BreakpointFlags flags);

#endif /* EXEC_WATCHPOINT_H */
