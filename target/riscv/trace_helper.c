/*
 * RISC-V Trace Support TCG helpers
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"
#include "trace.h"
#include "exec/helper-proto.h"

#ifndef CONFIG_USER_ONLY
#include "hw/riscv/trace-encoder.h"
#endif

#ifndef CONFIG_USER_ONLY
void helper_trace_insn(CPURISCVState *env, uint64_t pc)
{
    RISCVCPU *cpu = env_archcpu(env);
    TraceEncoder *te = TRACE_ENCODER(cpu->trencoder);

    if (te->trace_next_insn) {
        trencoder_set_first_trace_insn(cpu->trencoder, pc);
        te->trace_next_insn = false;
    }
}
#else /* #ifndef CONFIG_USER_ONLY */
void helper_trace_insn(CPURISCVState *env, uint64_t pc)
{
    return;
}
#endif /* #ifndef CONFIG_USER_ONLY*/
