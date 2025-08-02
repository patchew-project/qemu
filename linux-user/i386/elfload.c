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
    static const char elf_platform[4][5] = { "i386", "i486", "i586", "i686" };
    int family = object_property_get_int(OBJECT(cs), "family", NULL);

    family = MAX(MIN(family, 6), 3);
    return elf_platform[family - 3];
}

void elf_core_copy_regs(target_ulong *regs, const CPUX86State *env)
{
    regs[0] = tswapl(env->regs[R_EBX]);
    regs[1] = tswapl(env->regs[R_ECX]);
    regs[2] = tswapl(env->regs[R_EDX]);
    regs[3] = tswapl(env->regs[R_ESI]);
    regs[4] = tswapl(env->regs[R_EDI]);
    regs[5] = tswapl(env->regs[R_EBP]);
    regs[6] = tswapl(env->regs[R_EAX]);
    regs[7] = tswapl(env->segs[R_DS].selector & 0xffff);
    regs[8] = tswapl(env->segs[R_ES].selector & 0xffff);
    regs[9] = tswapl(env->segs[R_FS].selector & 0xffff);
    regs[10] = tswapl(env->segs[R_GS].selector & 0xffff);
    regs[11] = tswapl(get_task_state(env_cpu_const(env))->orig_ax);
    regs[12] = tswapl(env->eip);
    regs[13] = tswapl(env->segs[R_CS].selector & 0xffff);
    regs[14] = tswapl(env->eflags);
    regs[15] = tswapl(env->regs[R_ESP]);
    regs[16] = tswapl(env->segs[R_SS].selector & 0xffff);
}
