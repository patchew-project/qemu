/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "max";
}

abi_ulong get_elf_hwcap(CPUState *cs)
{
    return cpu_env(cs)->features[FEAT_1_EDX];
}

const char *get_elf_platform(CPUState *cs)
{
    return "x86_64";
}

void elf_core_copy_regs(target_ulong *regs, const CPUX86State *env)
{
    regs[0] = tswapl(env->regs[15]);
    regs[1] = tswapl(env->regs[14]);
    regs[2] = tswapl(env->regs[13]);
    regs[3] = tswapl(env->regs[12]);
    regs[4] = tswapl(env->regs[R_EBP]);
    regs[5] = tswapl(env->regs[R_EBX]);
    regs[6] = tswapl(env->regs[11]);
    regs[7] = tswapl(env->regs[10]);
    regs[8] = tswapl(env->regs[9]);
    regs[9] = tswapl(env->regs[8]);
    regs[10] = tswapl(env->regs[R_EAX]);
    regs[11] = tswapl(env->regs[R_ECX]);
    regs[12] = tswapl(env->regs[R_EDX]);
    regs[13] = tswapl(env->regs[R_ESI]);
    regs[14] = tswapl(env->regs[R_EDI]);
    regs[15] = tswapl(get_task_state(env_cpu_const(env))->orig_ax);
    regs[16] = tswapl(env->eip);
    regs[17] = tswapl(env->segs[R_CS].selector & 0xffff);
    regs[18] = tswapl(env->eflags);
    regs[19] = tswapl(env->regs[R_ESP]);
    regs[20] = tswapl(env->segs[R_SS].selector & 0xffff);
    regs[21] = tswapl(env->segs[R_FS].selector & 0xffff);
    regs[22] = tswapl(env->segs[R_GS].selector & 0xffff);
    regs[23] = tswapl(env->segs[R_DS].selector & 0xffff);
    regs[24] = tswapl(env->segs[R_ES].selector & 0xffff);
    regs[25] = tswapl(env->segs[R_FS].selector & 0xffff);
    regs[26] = tswapl(env->segs[R_GS].selector & 0xffff);
}
