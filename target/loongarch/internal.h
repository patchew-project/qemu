/*
 * LoongArch internal definitions and helpers.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef LOONGARCH_INTERNAL_H
#define LOONGARCH_INTERNAL_H

#include "exec/memattrs.h"

enum loongarch_mmu_types {
    MMU_TYPE_NONE,
    MMU_TYPE_LS3A5K,
};

struct loongarch_def_t {
    const char *name;
    int32_t FCSR0;
    int32_t FCSR0_MASK;
    int32_t PABITS;
    CPU_LOONGARCH_CSR
    uint64_t INSN_FLAGS;
    enum loongarch_mmu_types MMU_TYPE;
};

extern const struct loongarch_def_t loongarch_defs[];
extern const int loongarch_defs_number;

void loongarch_cpu_do_interrupt(CPUState *cpu);
bool loongarch_cpu_exec_interrupt(CPUState *cpu, int int_req);
void loongarch_cpu_dump_state(CPUState *cpu, FILE *f, int flags);

#define cpu_signal_handler cpu_loongarch_signal_handler

static inline bool cpu_loongarch_hw_interrupts_enabled(CPULoongArchState *env)
{
    bool ret = 0;

    ret = env->CSR_CRMD & (1 << CSR_CRMD_IE_SHIFT);

    return ret;
}

static inline bool cpu_loongarch_hw_interrupts_pending(CPULoongArchState *env)
{
    int32_t pending;
    int32_t status;
    bool r;

    pending = env->CSR_ESTAT & CSR_ESTAT_IPMASK;
    status  = env->CSR_ECFG & CSR_ECFG_IPMASK;

    r = (pending & status) != 0;
    return r;
}

void loongarch_tcg_init(void);

void QEMU_NORETURN do_raise_exception_err(CPULoongArchState *env,
                                          uint32_t exception,
                                          int error_code,
                                          uintptr_t pc);

static inline void QEMU_NORETURN do_raise_exception(CPULoongArchState *env,
                                                    uint32_t exception,
                                                    uintptr_t pc)
{
    do_raise_exception_err(env, exception, 0, pc);
}

static inline void restore_pamask(CPULoongArchState *env)
{
    if (env->hflags & LOONGARCH_HFLAG_ELPA) {
        env->PAMask = (1ULL << env->PABITS) - 1;
    } else {
        env->PAMask = PAMASK_BASE;
    }
}

static inline void compute_hflags(CPULoongArchState *env)
{
    env->hflags &= ~(LOONGARCH_HFLAG_64 | LOONGARCH_HFLAG_FPU |
                     LOONGARCH_HFLAG_KU | LOONGARCH_HFLAG_ELPA);

    env->hflags |= (env->CSR_CRMD & CSR_CRMD_PLV);
    env->hflags |= LOONGARCH_HFLAG_64;

    if (env->CSR_EUEN & CSR_EUEN_FPEN) {
        env->hflags |= LOONGARCH_HFLAG_FPU;
    }
}

const char *loongarch_exception_name(int32_t exception);

#endif
