/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/compiler.h"
#include "qom/cpu.h"
#include <stdbool.h>
#include <stdint.h>


extern unsigned int instr_cpus_count;
extern CPUState **instr_cpus;

static inline QICPU instr_cpu_to_qicpu(CPUState *vcpu)
{
    uintptr_t idx = vcpu->cpu_index;
    return (QICPU)idx;
}

static inline CPUState *instr_cpu_from_qicpu(QICPU vcpu)
{
    unsigned int idx = (uintptr_t)vcpu;
    if (idx >= instr_cpus_count) {
        return NULL;
    } else {
        return instr_cpus[idx];
    }
}


extern __thread InstrState instr_cur_state;

static inline void instr_set_state(InstrState state)
{
    atomic_store_release(&instr_cur_state, state);
}

static inline InstrState instr_get_state(void)
{
    return atomic_load_acquire(&instr_cur_state);
}
