/*
 * Instrumentation placeholders.
 *
 * Copyright (C) 2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* Unpoison missing types */
#define HW_POISON_H

#include "qemu/osdep.h"

#include "instrument/cmdline.h"
#include "instrument/control.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"


/* Declare missing types */
typedef struct CPUArchState CPUArchState;
typedef int target_ulong;


void instr_init(const char *path, int argc, const char **argv)
{
}
void instr_fini(void)
{
}

InstrLoadResult *qmp_instr_load(const char *path,
                                bool has_id, const char *id,
                                bool have_args, strList *args,
                                Error **errp);
InstrLoadResult *qmp_instr_load(const char *path,
                                bool has_id, const char *id,
                                bool have_args, strList *args,
                                Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
void qmp_instr_unload(const char *id, Error **errp);
void qmp_instr_unload(const char *id, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}


__thread InstrInfo instr_cur_info;
void (*instr_event__guest_cpu_enter)(QICPU *vcpu);
void (*instr_event__guest_cpu_exit)(QICPU *vcpu);
void (*instr_event__guest_cpu_reset)(QICPU *vcpu);
void (*instr_event__guest_mem_before_trans)(
    QICPU vcpu_trans, QITCGv_cpu vcpu_exec,
    QITCGv vaddr, QIMemInfo info);
void helper_instr_guest_mem_before_exec(
    CPUArchState *vcpu, target_ulong vaddr, uint32_t info);
void helper_instr_guest_mem_before_exec(
    CPUArchState *vcpu, target_ulong vaddr, uint32_t info)
{
    assert(false);
}
void (*instr_event__guest_mem_before_exec)(
    QICPU vcpu_trans, QITCGv_cpu vcpu_exec,
    QITCGv vaddr, QIMemInfo info);
