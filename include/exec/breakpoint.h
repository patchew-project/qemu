/*
 * QEMU breakpoint & watchpoint definitions
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EXEC_BREAKPOINT_H
#define EXEC_BREAKPOINT_H

#include "qemu/queue.h"
#include "exec/vaddr.h"

typedef struct CPUBreakpoint {
    vaddr pc;
    int flags; /* BP_* */
    QTAILQ_ENTRY(CPUBreakpoint) entry;
} CPUBreakpoint;

#endif
