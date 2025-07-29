/* SPDX-License-Identifier: GPL-2.0-or-later */

#define ELF_NREG    18
typedef target_elf_greg_t  target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUARMState *env)
{
    (*regs)[0] = tswapreg(env->regs[0]);
    (*regs)[1] = tswapreg(env->regs[1]);
    (*regs)[2] = tswapreg(env->regs[2]);
    (*regs)[3] = tswapreg(env->regs[3]);
    (*regs)[4] = tswapreg(env->regs[4]);
    (*regs)[5] = tswapreg(env->regs[5]);
    (*regs)[6] = tswapreg(env->regs[6]);
    (*regs)[7] = tswapreg(env->regs[7]);
    (*regs)[8] = tswapreg(env->regs[8]);
    (*regs)[9] = tswapreg(env->regs[9]);
    (*regs)[10] = tswapreg(env->regs[10]);
    (*regs)[11] = tswapreg(env->regs[11]);
    (*regs)[12] = tswapreg(env->regs[12]);
    (*regs)[13] = tswapreg(env->regs[13]);
    (*regs)[14] = tswapreg(env->regs[14]);
    (*regs)[15] = tswapreg(env->regs[15]);

    (*regs)[16] = tswapreg(cpsr_read((CPUARMState *)env));
    (*regs)[17] = tswapreg(env->regs[0]); /* XXX */
}
