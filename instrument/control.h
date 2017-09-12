/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef INSTRUMENT__CONTROL_H
#define INSTRUMENT__CONTROL_H

#include "qemu/typedefs.h"
#include "instrument/qemu-instr/types.h"


/**
 * instr_cpu_add:
 *
 * Make @vcpu available to instrumentation clients.
 *
 * Precondition: cpu_list_lock().
 */
void instr_cpu_add(CPUState *vcpu);

/**
 * instr_cpu_remove:
 *
 * Make @vcpu unavailable to instrumentation clients.
 *
 * Precondition: cpu_list_lock().
 */
void instr_cpu_remove(CPUState *vcpu);

/**
 * instr_cpu_to_qicpu:
 *
 * Get the #QICPU corresponding to the given #CPUState.
 */
static inline QICPU instr_cpu_to_qicpu(CPUState *vcpu);

/**
 * instr_cpu_from_qicpu:
 *
 * Get the #CPUState corresponding to the given #QICPU.
 */
static inline CPUState *instr_cpu_from_qicpu(QICPU vcpu);


/**
 * InstrState:
 * @INSTR_STATE_DISABLE: Intrumentation API not available.
 * @INSTR_STATE_ENABLE: Intrumentation API available.
 *
 * Instrumentation state of current host thread. Used to ensure instrumentation
 * clients use QEMU's API only in expected points.
 */
typedef enum {
    INSTR_STATE_DISABLE,
    INSTR_STATE_ENABLE,
} InstrState;

/**
 * instr_set_state:
 *
 * Set the instrumentation state of the current host thread.
 */
static inline void instr_set_state(InstrState state);

/**
 * instr_get_state:
 *
 * Get the instrumentation state of the current host thread.
 */
static inline InstrState instr_get_state(void);


#include "instrument/control.inc.h"

#endif  /* INSTRUMENT__CONTROL_H */
