/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel arch/openrisc/include/asm/elf.h.  */
#define ELF_NREG 34 /* gprs and pc, sr */
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUOpenRISCState *env)
{
    for (int i = 0; i < 32; i++) {
        (*regs)[i] = tswapreg(cpu_get_gpr(env, i));
    }
    (*regs)[32] = tswapreg(env->pc);
    (*regs)[33] = tswapreg(cpu_get_sr(env));
}
