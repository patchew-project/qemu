/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    if (eflags == 0 || (eflags & EF_M68K_M68000)) {
        /* 680x0 */
        return "m68040";
    }

    /* Coldfire */
    return "any";
}

void elf_core_copy_regs(target_ulong *regs, const CPUM68KState *env)
{
    regs[0] = tswapl(env->dregs[1]);
    regs[1] = tswapl(env->dregs[2]);
    regs[2] = tswapl(env->dregs[3]);
    regs[3] = tswapl(env->dregs[4]);
    regs[4] = tswapl(env->dregs[5]);
    regs[5] = tswapl(env->dregs[6]);
    regs[6] = tswapl(env->dregs[7]);
    regs[7] = tswapl(env->aregs[0]);
    regs[8] = tswapl(env->aregs[1]);
    regs[9] = tswapl(env->aregs[2]);
    regs[10] = tswapl(env->aregs[3]);
    regs[11] = tswapl(env->aregs[4]);
    regs[12] = tswapl(env->aregs[5]);
    regs[13] = tswapl(env->aregs[6]);
    regs[14] = tswapl(env->dregs[0]);
    regs[15] = tswapl(env->aregs[7]);
    regs[16] = tswapl(env->dregs[0]); /* FIXME: orig_d0 */
    regs[17] = tswapl(env->sr);
    regs[18] = tswapl(env->pc);
    regs[19] = 0;  /* FIXME: regs->format | regs->vector */
}
