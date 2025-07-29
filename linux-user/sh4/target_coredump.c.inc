/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/sh/include/asm/elf.h.  */
#define ELF_NREG 23
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/sh/include/asm/ptrace.h.  */
enum {
    TARGET_REG_PC = 16,
    TARGET_REG_PR = 17,
    TARGET_REG_SR = 18,
    TARGET_REG_GBR = 19,
    TARGET_REG_MACH = 20,
    TARGET_REG_MACL = 21,
    TARGET_REG_SYSCALL = 22
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUSH4State *env)
{
    for (int i = 0; i < 16; i++) {
        (*regs)[i] = tswapreg(env->gregs[i]);
    }

    (*regs)[TARGET_REG_PC] = tswapreg(env->pc);
    (*regs)[TARGET_REG_PR] = tswapreg(env->pr);
    (*regs)[TARGET_REG_SR] = tswapreg(env->sr);
    (*regs)[TARGET_REG_GBR] = tswapreg(env->gbr);
    (*regs)[TARGET_REG_MACH] = tswapreg(env->mach);
    (*regs)[TARGET_REG_MACL] = tswapreg(env->macl);
    (*regs)[TARGET_REG_SYSCALL] = 0; /* FIXME */
}
