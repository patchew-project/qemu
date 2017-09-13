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
        InstrInfo *iinfo = instr_set_state(INSTR_STATE_ENABLE_TCG);
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

static inline void instr_guest_mem_before_exec(
    CPUState *vcpu, uint64_t vaddr, TraceMemInfo info)
{
    void (*cb)(QICPU vcpu, uint64_t vaddr, QIMemInfo info)
        = instr_get_event(guest_mem_before_exec);
    if (cb) {
        InstrInfo *iinfo = instr_set_state(INSTR_STATE_ENABLE);
        QICPU vcpu_ = instr_cpu_to_qicpu(vcpu);
        QIMemInfo info_;
        info_.raw = info.raw;
        instr_tcg_count(iinfo, 2);
        (*cb)(vcpu_, vaddr, info_);
        instr_set_state(INSTR_STATE_DISABLE);
    }
}

static inline void instr_guest_user_syscall(
    CPUState *vcpu, uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
    uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8)
{
    void (*cb)(QICPU vcpu, uint64_t num, uint64_t arg1, uint64_t arg2,
               uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6,
               uint64_t arg7, uint64_t arg8)
        = instr_get_event(guest_user_syscall);
    if (cb) {
        instr_set_state(INSTR_STATE_ENABLE);
        QICPU vcpu_ = instr_cpu_to_qicpu(vcpu);
        (*cb)(vcpu_, num, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
        instr_set_state(INSTR_STATE_DISABLE);
    }
}
