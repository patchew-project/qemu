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

typedef struct InstrCPUStop InstrCPUStop;
typedef void (*instr_cpu_stop_fun)(CPUState *cpu, void *data);

/**
 * instr_cpu_stop_all_begin:
 * @info: Opaque structure describing the operation.
 * @fun: Function to run on the context of each vCPU once stopped.
 * @data: Pointer to pass to @fun.
 *
 * Ensure all vCPUs stop executing guest code, and execute @fun on their context
 * in turn. Returns with all vCPUs still stopped.
 *
 * Assumes cpu_list_lock() and that the QBL is locked before calling.
 */
void instr_cpu_stop_all_begin(InstrCPUStop *info,
                              instr_cpu_stop_fun fun, void *data);

/**
 * instr_cpu_stop_all_end:
 * @info: Opaque structure passed to a previous instr_cpu_stop_all_begin()
 *     call.
 *
 * Resume execution on all vCPUs stopped by instr_cpu_stop_all_begin().
 */
void instr_cpu_stop_all_end(InstrCPUStop *info);


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
    INSTR_STATE_ENABLE_TCG,
} InstrState;

#define INSTR_MAX_TCG_REGS 16

typedef struct InstrInfo {
    InstrState state;
    unsigned int max;
    void *tcg_regs[INSTR_MAX_TCG_REGS];
} InstrInfo;

/**
 * instr_set_state:
 *
 * Set the instrumentation state of the current host thread, and return its
 * #InstrInfo.
 */
static inline InstrInfo *instr_set_state(InstrState state);

/**
 * instr_get_state:
 *
 * Get the instrumentation state of the current host thread.
 */
static inline InstrState instr_get_state(void);

/**
 * instr_tcg_to_qitcg:
 * @info: Pointer to #InstrInfo.
 * @num: Number of TCG register used by instrumentation.
 * @arg: TCG register.
 *
 * Get a suitable QITCGv* from a TCGv* value.
 */
#define instr_tcg_to_qitcg(info, num, arg) \
    ({                                \
        info->tcg_regs[num] = arg;    \
        (void *)num;                  \
    })

/**
 * instr_tcg_from_qitcg:
 * @info: Pointer to #InstrInfo.
 * @arg: QITCG register.
 *
 * Get a suitable TCGv* from a QITCGv* value.
 */
#define instr_tcg_from_qitcg(info, arg)                                \
    ({                                                          \
        unsigned int idx = (uintptr_t)arg;                      \
        ERROR_IF(info->max <= idx, "invalid QITCGv register");  \
        info->tcg_regs[idx];                                  \
    })

/**
 * instr_tcg_count:
 * @info: Pointer to #InstrInfo.
 * @count: Number of TCG registers used by instrumentation.
 *
 * Set the number of TCG registers used by instrumentation.
 */
static inline void instr_tcg_count(InstrInfo *info, unsigned int count);


#include "instrument/control.inc.h"

#endif  /* INSTRUMENT__CONTROL_H */
