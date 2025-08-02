/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return XTENSA_DEFAULT_CPU_MODEL;
}

enum {
    TARGET_REG_PC,
    TARGET_REG_PS,
    TARGET_REG_LBEG,
    TARGET_REG_LEND,
    TARGET_REG_LCOUNT,
    TARGET_REG_SAR,
    TARGET_REG_WINDOWSTART,
    TARGET_REG_WINDOWBASE,
    TARGET_REG_THREADPTR,
    TARGET_REG_AR0 = 64,
};

void elf_core_copy_regs(target_ulong *regs, const CPUXtensaState *env)
{
    unsigned i;

    regs[TARGET_REG_PC] = tswapl(env->pc);
    regs[TARGET_REG_PS] = tswapl(env->sregs[PS] & ~PS_EXCM);
    regs[TARGET_REG_LBEG] = tswapl(env->sregs[LBEG]);
    regs[TARGET_REG_LEND] = tswapl(env->sregs[LEND]);
    regs[TARGET_REG_LCOUNT] = tswapl(env->sregs[LCOUNT]);
    regs[TARGET_REG_SAR] = tswapl(env->sregs[SAR]);
    regs[TARGET_REG_WINDOWSTART] = tswapl(env->sregs[WINDOW_START]);
    regs[TARGET_REG_WINDOWBASE] = tswapl(env->sregs[WINDOW_BASE]);
    regs[TARGET_REG_THREADPTR] = tswapl(env->uregs[THREADPTR]);
    xtensa_sync_phys_from_window((CPUXtensaState *)env);
    for (i = 0; i < env->config->nareg; ++i) {
        regs[TARGET_REG_AR0 + i] = tswapl(env->phys_regs[i]);
    }
}
