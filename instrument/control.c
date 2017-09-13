/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "instrument/control.h"
#include "instrument/error.h"
#include "instrument/events.h"
#include "instrument/load.h"
#include "instrument/qemu-instr/control.h"
#include "qemu/compiler.h"
#include "qom/cpu.h"


__thread InstrState instr_cur_state;


unsigned int instr_cpus_count;
CPUState **instr_cpus;

void instr_cpu_add(CPUState *vcpu)
{
    unsigned int idx = vcpu->cpu_index;
    if (idx >= instr_cpus_count) {
        instr_cpus_count = idx + 1;
        instr_cpus = realloc(instr_cpus,
                             sizeof(*instr_cpus) * instr_cpus_count);
    }
    instr_cpus[idx] = vcpu;
}

void instr_cpu_remove(CPUState *vcpu)
{
    unsigned int idx = vcpu->cpu_index;
    instr_cpus[idx] = NULL;
}


qi_fini_fn instr_event__fini_fn;
void *instr_event__fini_data;

SYM_PUBLIC void qi_set_fini(qi_fini_fn fn, void *data)
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    instr_set_event(fini_fn, fn);
    instr_set_event(fini_data, data);
}


void (*instr_event__guest_cpu_enter)(QICPU vcpu);

SYM_PUBLIC void qi_event_set_guest_cpu_enter(void (*fn)(QICPU vcpu))
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    instr_set_event(guest_cpu_enter, fn);
}
