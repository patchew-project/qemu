/*
 * Internal API for triggering instrumentation events.
 *
 * Copyright (C) 2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef INSTRUMENT__EVENTS_H
#define INSTRUMENT__EVENTS_H

#include "instrument/qemu-instr/control.h"
#include "instrument/qemu-instr/types.h"
#include "trace/control.h"


/**
 * instr_get_event:
 *
 * Get value set by instrumentation library.
 */
#define instr_get_event(name)                   \
    atomic_load_acquire(&instr_event__ ## name)

/**
 * instr_get_event:
 *
 * Set value from instrumentation library.
 */
#define instr_set_event(name, fn)               \
    atomic_store_release(&instr_event__ ## name, fn)


/*
 * Re-define types used by some instrumentation events. We need some arbitrary
 * definition for non-target objects.
 */
#if defined(QEMU_TARGET_BUILD)
#include "tcg/tcg.h"
#else
typedef struct TCGv_d *TCGv;
typedef struct TCGv_env_d *TCGv_env;
typedef struct TCGv_i32_d *TCGv_i32;
typedef struct TCGv_i64_d *TCGv_i64;
#endif


extern qi_fini_fn instr_event__fini_fn;
extern void *instr_event__fini_data;

extern void (*instr_event__guest_cpu_enter)(QICPU vcpu);
static inline void instr_guest_cpu_enter(CPUState *vcpu);

extern void (*instr_event__guest_cpu_exit)(QICPU vcpu);
static inline void instr_guest_cpu_exit(CPUState *vcpu);

extern void (*instr_event__guest_cpu_reset)(QICPU vcpu);
static inline void instr_guest_cpu_reset(CPUState *vcpu);

extern void (*instr_event__guest_mem_before_trans)(
    QICPU vcpu_trans, QITCGv_cpu vcpu_exec, QITCGv vaddr, QIMemInfo info);
static inline void instr_guest_mem_before_trans(
    CPUState *vcpu_trans, TCGv_env vcpu_exec, TCGv vaddr, TraceMemInfo info);

extern void (*instr_event__guest_mem_before_exec)(
    QICPU vcpu, uint64_t vaddr, QIMemInfo info);
static inline void instr_guest_mem_before_exec(
    CPUState *vcpu, uint64_t vaddr, TraceMemInfo info);

extern void (*instr_event__guest_user_syscall)(
    QICPU vcpu, uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
    uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8);
static inline void instr_guest_user_syscall(
    CPUState *vcpu, uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
    uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8);


#include "instrument/events.inc.h"

#endif  /* INSTRUMENT__EVENTS_H */
