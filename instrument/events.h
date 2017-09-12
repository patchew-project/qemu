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


extern qi_fini_fn instr_event__fini_fn;
extern void *instr_event__fini_data;

extern void (*instr_event__guest_cpu_enter)(QICPU vcpu);
static inline void instr_guest_cpu_enter(CPUState *vcpu);

extern void (*instr_event__guest_cpu_exit)(QICPU vcpu);
static inline void instr_guest_cpu_exit(CPUState *vcpu);

extern void (*instr_event__guest_cpu_reset)(QICPU vcpu);
static inline void instr_guest_cpu_reset(CPUState *vcpu);


#include "instrument/events.inc.h"

#endif  /* INSTRUMENT__EVENTS_H */
