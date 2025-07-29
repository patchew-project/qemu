/* SPDX-License-Identifier: GPL-2.0-or-later */

#define ELF_NREG 38
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
static void elf_core_copy_regs(target_elf_gregset_t *regs, const CPUMBState *env)
{
    int i, pos = 0;

    for (i = 0; i < 32; i++) {
        (*regs)[pos++] = tswapreg(env->regs[i]);
    }

    (*regs)[pos++] = tswapreg(env->pc);
    (*regs)[pos++] = tswapreg(mb_cpu_read_msr(env));
    (*regs)[pos++] = 0;
    (*regs)[pos++] = tswapreg(env->ear);
    (*regs)[pos++] = 0;
    (*regs)[pos++] = tswapreg(env->esr);
}
