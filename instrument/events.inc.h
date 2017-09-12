/*
 * Internal API for triggering instrumentation events.
 *
 * Copyright (C) 2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "instrument/control.h"


static inline void instr_guest_cpu_enter(CPUState *vcpu)
{
    void (*cb)(QICPU vcpu) = instr_get_event(guest_cpu_enter);
    if (cb) {
        QICPU vcpu_ = instr_cpu_to_qicpu(vcpu);
        instr_set_state(INSTR_STATE_ENABLE);
        (*cb)(vcpu_);
        instr_set_state(INSTR_STATE_DISABLE);
    }
}

static inline void instr_guest_cpu_exit(CPUState *vcpu)
{
    void (*cb)(QICPU vcpu) = instr_get_event(guest_cpu_exit);
    if (cb) {
        QICPU vcpu_ = instr_cpu_to_qicpu(vcpu);
        instr_set_state(INSTR_STATE_ENABLE);
        (*cb)(vcpu_);
        instr_set_state(INSTR_STATE_DISABLE);
    }
}
