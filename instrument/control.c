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
#include "qemu/main-loop.h"
#include "qom/cpu.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"


__thread InstrInfo instr_cur_info;


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


static void instr_cpu_stop_all__cb(CPUState *cpu, run_on_cpu_data data)
{
    InstrCPUStop *info = data.host_ptr;
    /* run posted function */
    if (info->fun) {
        info->fun(cpu, info->data);
    }
#if !defined(CONFIG_USER_ONLY)
    /* signal we're out of the main vCPU loop */
    unsigned int count = atomic_load_acquire(&info->count);
    atomic_store_release(&info->count, count + 1);
    atomic_store_release(&info->stopped, true);
    /* wait until we're good to go again */
    qemu_cond_wait(&info->cond, &info->mutex);
    count = atomic_load_acquire(&info->count);
    atomic_store_release(&info->count, count - 1);
    qemu_mutex_unlock(&info->mutex);
#endif
}

void instr_cpu_stop_all_begin(InstrCPUStop *info,
                              instr_cpu_stop_fun fun, void *data)
{
    CPUState *cpu;

    info->fun = fun;
    info->data = data;

#if !defined(CONFIG_USER_ONLY)
    info->count = 0;
    qemu_cond_init(&info->cond);
    qemu_mutex_init(&info->mutex);

    /* main dispatch loop and run_on_cpu() lock the BQL */
    qemu_mutex_unlock_iothread();
#endif

    CPU_FOREACH(cpu) {
#if !defined(CONFIG_USER_ONLY)
        atomic_store_release(&info->stopped, false);
        qemu_mutex_lock(&info->mutex);
        async_run_on_cpu(cpu, instr_cpu_stop_all__cb,
                         RUN_ON_CPU_HOST_PTR(info));
        while (!atomic_load_acquire(&info->stopped)) {
            /* wait for vCPU to signal it's stopped */
        }
#else
        instr_cpu_stop_all__cb(cpu, RUN_ON_CPU_HOST_PTR(info));
#endif
    }
}

void instr_cpu_stop_all_end(InstrCPUStop *info)
{
#if !defined(CONFIG_USER_ONLY)
    qemu_cond_broadcast(&info->cond);
    while (atomic_load_acquire(&info->count)) {
        /* wait for all vCPUs to continue before we can destroy info */
    }
    qemu_cond_destroy(&info->cond);
    qemu_mutex_destroy(&info->mutex);
    qemu_mutex_lock_iothread();
#endif
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


void (*instr_event__guest_cpu_exit)(QICPU vcpu);

SYM_PUBLIC void qi_event_set_guest_cpu_exit(void (*fn)(QICPU vcpu))
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    instr_set_event(guest_cpu_exit, fn);
}


void (*instr_event__guest_cpu_reset)(QICPU vcpu);

SYM_PUBLIC void qi_event_set_guest_cpu_reset(void (*fn)(QICPU vcpu))
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    instr_set_event(guest_cpu_reset, fn);
}


void (*instr_event__guest_mem_before_trans)(
    QICPU vcpu_trans, QITCGv_cpu vcpu_exec, QITCGv vaddr, QIMemInfo info);

SYM_PUBLIC void qi_event_set_guest_mem_before_trans(
    void (*fn)(QICPU vcpu_trans, QITCGv_cpu vcpu_exec,
               QITCGv vaddr, QIMemInfo info))
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    ERROR_IF(!tcg_enabled(), "called without TCG");
    instr_set_event(guest_mem_before_trans, fn);
}


SYM_PUBLIC void qi_event_gen_guest_mem_before_exec(
    QITCGv_cpu vcpu, QITCGv vaddr, QIMemInfo info)
{
    ERROR_IF(instr_get_state() != INSTR_STATE_ENABLE_TCG,
             "called outside instrumentation");
    ERROR_IF(!tcg_enabled(), "called without TCG");
    InstrInfo *iinfo = &instr_cur_info;
    TCGv_env vcpu_ = instr_tcg_from_qitcg(iinfo, vcpu);
    TCGv vaddr_ = instr_tcg_from_qitcg(iinfo, vaddr);
    TCGv_i32 info_ = tcg_const_i32(info.raw);
    gen_helper_instr_guest_mem_before_exec(vcpu_, vaddr_, info_);
    tcg_temp_free_i32(info_);
}

void helper_instr_guest_mem_before_exec(
    CPUArchState *vcpu, target_ulong vaddr, uint32_t info)
{
    TraceMemInfo info_;
    info_.raw = info;
    instr_guest_mem_before_exec(ENV_GET_CPU(vcpu), vaddr, info_);
}


void (*instr_event__guest_mem_before_exec)(
    QICPU vcpu, uint64_t vaddr, QIMemInfo info);

SYM_PUBLIC void qi_event_set_guest_mem_before_exec(
    void (*fn)(QICPU vcpu, uint64_t vaddr, QIMemInfo info))
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    ERROR_IF(!tcg_enabled(), "called without TCG");
    instr_set_event(guest_mem_before_exec, fn);
}


void (*instr_event__guest_user_syscall)(
    QICPU vcpu, uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
    uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8);

SYM_PUBLIC void qi_event_set_guest_user_syscall(
    void (*fn)(QICPU vcpu, uint64_t num, uint64_t arg1, uint64_t arg2,
               uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6,
               uint64_t arg7, uint64_t arg8))
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    ERROR_IF(!tcg_enabled(), "called without TCG");
#if !defined(CONFIG_USER_ONLY)
    ERROR_IF(true, "called in full-system mode");
#endif
    instr_set_event(guest_user_syscall, fn);
}
