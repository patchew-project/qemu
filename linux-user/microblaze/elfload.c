/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
void elf_core_copy_regs(target_ulong *regs, const CPUMBState *env)
{
    int i, pos = 0;

    for (i = 0; i < 32; i++) {
        regs[pos++] = tswapl(env->regs[i]);
    }

    regs[pos++] = tswapl(env->pc);
    regs[pos++] = tswapl(mb_cpu_read_msr(env));
    regs[pos++] = 0;
    regs[pos++] = tswapl(env->ear);
    regs[pos++] = 0;
    regs[pos++] = tswapl(env->esr);
}
