/*
 * Internal API for triggering instrumentation events.
 *
 * Copyright (C) 2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "instrument/control.h"
#include "trace/control.h"


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

static inline void instr_guest_cpu_reset(CPUState *vcpu)
{
    void (*cb)(QICPU vcpu) = instr_get_event(guest_cpu_reset);
    if (cb) {
        QICPU vcpu_ = instr_cpu_to_qicpu(vcpu);
        instr_set_state(INSTR_STATE_ENABLE);
        (*cb)(vcpu_);
        instr_set_state(INSTR_STATE_DISABLE);
    }
}

static inline void instr_guest_mem_before_trans(
    CPUState *vcpu_trans, TCGv_env vcpu_exec, TCGv vaddr, TraceMemInfo info)
{
    void (*cb)(QICPU vcpu_trans, QITCGv_cpu vcpu_exec,
               QITCGv vaddr, QIMemInfo info)
        = instr_get_event(guest_mem_before_trans);
    if (cb) {
        InstrInfo *iinfo = instr_set_state(INSTR_STATE_ENABLE);
        QICPU vcpu_trans_ = instr_cpu_to_qicpu(vcpu_trans);
        QITCGv_cpu vcpu_exec_ = instr_tcg_to_qitcg(iinfo, 0, vcpu_exec);
        QITCGv vaddr_ = instr_tcg_to_qitcg(iinfo, 1, vaddr);
        QIMemInfo info_;
        info_.raw = info.raw;
        instr_tcg_count(iinfo, 2);
        (*cb)(vcpu_trans_, vcpu_exec_, vaddr_, info_);
        instr_set_state(INSTR_STATE_DISABLE);
    }
}
