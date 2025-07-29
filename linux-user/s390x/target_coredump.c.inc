/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/s390/include/uapi/asm/ptrace.h (s390_regs).  */
#define ELF_NREG 27
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

enum {
    TARGET_REG_PSWM = 0,
    TARGET_REG_PSWA = 1,
    TARGET_REG_GPRS = 2,
    TARGET_REG_ARS = 18,
    TARGET_REG_ORIG_R2 = 26,
};

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUS390XState *env)
{
    int i;
    uint32_t *aregs;

    (*regs)[TARGET_REG_PSWM] = tswapreg(env->psw.mask);
    (*regs)[TARGET_REG_PSWA] = tswapreg(env->psw.addr);
    for (i = 0; i < 16; i++) {
        (*regs)[TARGET_REG_GPRS + i] = tswapreg(env->regs[i]);
    }
    aregs = (uint32_t *)&((*regs)[TARGET_REG_ARS]);
    for (i = 0; i < 16; i++) {
        aregs[i] = tswap32(env->aregs[i]);
    }
    (*regs)[TARGET_REG_ORIG_R2] = 0;
}
