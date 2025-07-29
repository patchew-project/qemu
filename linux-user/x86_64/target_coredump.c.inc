/* SPDX-License-Identifier: GPL-2.0-or-later */

#define ELF_NREG    27
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

/*
 * Note that ELF_NREG should be 29 as there should be place for
 * TRAPNO and ERR "registers" as well but linux doesn't dump those.
 *
 * See linux kernel: arch/x86/include/asm/elf.h
 */
static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUX86State *env)
{
    (*regs)[0] = tswapreg(env->regs[15]);
    (*regs)[1] = tswapreg(env->regs[14]);
    (*regs)[2] = tswapreg(env->regs[13]);
    (*regs)[3] = tswapreg(env->regs[12]);
    (*regs)[4] = tswapreg(env->regs[R_EBP]);
    (*regs)[5] = tswapreg(env->regs[R_EBX]);
    (*regs)[6] = tswapreg(env->regs[11]);
    (*regs)[7] = tswapreg(env->regs[10]);
    (*regs)[8] = tswapreg(env->regs[9]);
    (*regs)[9] = tswapreg(env->regs[8]);
    (*regs)[10] = tswapreg(env->regs[R_EAX]);
    (*regs)[11] = tswapreg(env->regs[R_ECX]);
    (*regs)[12] = tswapreg(env->regs[R_EDX]);
    (*regs)[13] = tswapreg(env->regs[R_ESI]);
    (*regs)[14] = tswapreg(env->regs[R_EDI]);
    (*regs)[15] = tswapreg(get_task_state(env_cpu_const(env))->orig_ax);
    (*regs)[16] = tswapreg(env->eip);
    (*regs)[17] = tswapreg(env->segs[R_CS].selector & 0xffff);
    (*regs)[18] = tswapreg(env->eflags);
    (*regs)[19] = tswapreg(env->regs[R_ESP]);
    (*regs)[20] = tswapreg(env->segs[R_SS].selector & 0xffff);
    (*regs)[21] = tswapreg(env->segs[R_FS].selector & 0xffff);
    (*regs)[22] = tswapreg(env->segs[R_GS].selector & 0xffff);
    (*regs)[23] = tswapreg(env->segs[R_DS].selector & 0xffff);
    (*regs)[24] = tswapreg(env->segs[R_ES].selector & 0xffff);
    (*regs)[25] = tswapreg(env->segs[R_FS].selector & 0xffff);
    (*regs)[26] = tswapreg(env->segs[R_GS].selector & 0xffff);
}
