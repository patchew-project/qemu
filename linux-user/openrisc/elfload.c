/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}

void elf_core_copy_regs(target_ulong *regs, const CPUOpenRISCState *env)
{
    int i;

    for (i = 0; i < 32; i++) {
        regs[i] = tswapl(cpu_get_gpr(env, i));
    }
    regs[32] = tswapl(env->pc);
    regs[33] = tswapl(cpu_get_sr(env));
}
