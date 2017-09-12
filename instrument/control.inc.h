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


struct InstrCPUStop {
    instr_cpu_stop_fun fun;
    void *data;
#if !defined(CONFIG_USER_ONLY)
    bool stopped;
    unsigned int count;
    QemuCond cond;
    QemuMutex mutex;
#endif
};

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


extern __thread InstrInfo instr_cur_info;

static inline InstrInfo *instr_set_state(InstrState state)
{
    InstrInfo *info = &instr_cur_info;
    atomic_store_release(&info->state, state);
    return info;
}

static inline InstrState instr_get_state(void)
{
    return atomic_load_acquire(&instr_cur_info.state);
}


static inline void instr_tcg_count(InstrInfo *info, unsigned int count)
{
    info->max = count;
}
