/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/loongarch/include/asm/elf.h */
#define ELF_NREG 45
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

enum {
    TARGET_EF_R0 = 0,
    TARGET_EF_CSR_ERA = TARGET_EF_R0 + 33,
    TARGET_EF_CSR_BADV = TARGET_EF_R0 + 34,
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPULoongArchState *env)
{
    (*regs)[TARGET_EF_R0] = 0;

    for (int i = 1; i < ARRAY_SIZE(env->gpr); i++) {
        (*regs)[TARGET_EF_R0 + i] = tswapreg(env->gpr[i]);
    }

    (*regs)[TARGET_EF_CSR_ERA] = tswapreg(env->pc);
    (*regs)[TARGET_EF_CSR_BADV] = tswapreg(env->CSR_BADV);
}
