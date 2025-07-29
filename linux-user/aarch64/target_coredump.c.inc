/* SPDX-License-Identifier: GPL-2.0-or-later */

#define ELF_NREG    34
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUARMState *env)
{
    for (int i = 0; i < 32; i++) {
        (*regs)[i] = tswapreg(env->xregs[i]);
    }
    (*regs)[32] = tswapreg(env->pc);
    (*regs)[33] = tswapreg(pstate_read((CPUARMState *)env));
}
