/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/m68k/include/asm/elf.h.  */
#define ELF_NREG 20
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUM68KState *env)
{
    (*regs)[0] = tswapreg(env->dregs[1]);
    (*regs)[1] = tswapreg(env->dregs[2]);
    (*regs)[2] = tswapreg(env->dregs[3]);
    (*regs)[3] = tswapreg(env->dregs[4]);
    (*regs)[4] = tswapreg(env->dregs[5]);
    (*regs)[5] = tswapreg(env->dregs[6]);
    (*regs)[6] = tswapreg(env->dregs[7]);
    (*regs)[7] = tswapreg(env->aregs[0]);
    (*regs)[8] = tswapreg(env->aregs[1]);
    (*regs)[9] = tswapreg(env->aregs[2]);
    (*regs)[10] = tswapreg(env->aregs[3]);
    (*regs)[11] = tswapreg(env->aregs[4]);
    (*regs)[12] = tswapreg(env->aregs[5]);
    (*regs)[13] = tswapreg(env->aregs[6]);
    (*regs)[14] = tswapreg(env->dregs[0]);
    (*regs)[15] = tswapreg(env->aregs[7]);
    (*regs)[16] = tswapreg(env->dregs[0]); /* FIXME: orig_d0 */
    (*regs)[17] = tswapreg(env->sr);
    (*regs)[18] = tswapreg(env->pc);
    (*regs)[19] = 0;  /* FIXME: regs->format | regs->vector */
}
