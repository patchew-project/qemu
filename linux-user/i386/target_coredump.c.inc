/* SPDX-License-Identifier: GPL-2.0-or-later */

#define ELF_NREG    17
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

/*
 * Note that ELF_NREG should be 19 as there should be place for
 * TRAPNO and ERR "registers" as well but linux doesn't dump those.
 *
 * See linux kernel: arch/x86/include/asm/elf.h
 */
static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUX86State *env)
{
    (*regs)[0] = tswapreg(env->regs[R_EBX]);
    (*regs)[1] = tswapreg(env->regs[R_ECX]);
    (*regs)[2] = tswapreg(env->regs[R_EDX]);
    (*regs)[3] = tswapreg(env->regs[R_ESI]);
    (*regs)[4] = tswapreg(env->regs[R_EDI]);
    (*regs)[5] = tswapreg(env->regs[R_EBP]);
    (*regs)[6] = tswapreg(env->regs[R_EAX]);
    (*regs)[7] = tswapreg(env->segs[R_DS].selector & 0xffff);
    (*regs)[8] = tswapreg(env->segs[R_ES].selector & 0xffff);
    (*regs)[9] = tswapreg(env->segs[R_FS].selector & 0xffff);
    (*regs)[10] = tswapreg(env->segs[R_GS].selector & 0xffff);
    (*regs)[11] = tswapreg(get_task_state(env_cpu_const(env))->orig_ax);
    (*regs)[12] = tswapreg(env->eip);
    (*regs)[13] = tswapreg(env->segs[R_CS].selector & 0xffff);
    (*regs)[14] = tswapreg(env->eflags);
    (*regs)[15] = tswapreg(env->regs[R_ESP]);
    (*regs)[16] = tswapreg(env->segs[R_SS].selector & 0xffff);
}
