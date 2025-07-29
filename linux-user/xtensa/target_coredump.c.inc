/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/xtensa/include/asm/elf.h.  */
#define ELF_NREG 128
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

enum {
    TARGET_REG_PC,
    TARGET_REG_PS,
    TARGET_REG_LBEG,
    TARGET_REG_LEND,
    TARGET_REG_LCOUNT,
    TARGET_REG_SAR,
    TARGET_REG_WINDOWSTART,
    TARGET_REG_WINDOWBASE,
    TARGET_REG_THREADPTR,
    TARGET_REG_AR0 = 64,
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUXtensaState *env)
{
    unsigned i;

    (*regs)[TARGET_REG_PC] = tswapreg(env->pc);
    (*regs)[TARGET_REG_PS] = tswapreg(env->sregs[PS] & ~PS_EXCM);
    (*regs)[TARGET_REG_LBEG] = tswapreg(env->sregs[LBEG]);
    (*regs)[TARGET_REG_LEND] = tswapreg(env->sregs[LEND]);
    (*regs)[TARGET_REG_LCOUNT] = tswapreg(env->sregs[LCOUNT]);
    (*regs)[TARGET_REG_SAR] = tswapreg(env->sregs[SAR]);
    (*regs)[TARGET_REG_WINDOWSTART] = tswapreg(env->sregs[WINDOW_START]);
    (*regs)[TARGET_REG_WINDOWBASE] = tswapreg(env->sregs[WINDOW_BASE]);
    (*regs)[TARGET_REG_THREADPTR] = tswapreg(env->uregs[THREADPTR]);
    xtensa_sync_phys_from_window((CPUXtensaState *)env);
    for (i = 0; i < env->config->nareg; ++i) {
        (*regs)[TARGET_REG_AR0 + i] = tswapreg(env->phys_regs[i]);
    }
}
