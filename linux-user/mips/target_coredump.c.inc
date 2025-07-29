/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/mips/include/asm/elf.h.  */
#define ELF_NREG 45
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

/* See linux kernel: arch/mips/include/asm/reg.h.  */
enum {
#ifdef TARGET_MIPS64
    TARGET_EF_R0 = 0,
#else
    TARGET_EF_R0 = 6,
#endif
    TARGET_EF_R26 = TARGET_EF_R0 + 26,
    TARGET_EF_R27 = TARGET_EF_R0 + 27,
    TARGET_EF_LO = TARGET_EF_R0 + 32,
    TARGET_EF_HI = TARGET_EF_R0 + 33,
    TARGET_EF_CP0_EPC = TARGET_EF_R0 + 34,
    TARGET_EF_CP0_BADVADDR = TARGET_EF_R0 + 35,
    TARGET_EF_CP0_STATUS = TARGET_EF_R0 + 36,
    TARGET_EF_CP0_CAUSE = TARGET_EF_R0 + 37
};

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUMIPSState *env)
{
    for (int i = 0; i < TARGET_EF_R0; i++) {
        (*regs)[i] = 0;
    }
    (*regs)[TARGET_EF_R0] = 0;


    for (int i = 1; i < ARRAY_SIZE(env->active_tc.gpr); i++) {
        (*regs)[TARGET_EF_R0 + i] = tswapreg(env->active_tc.gpr[i]);
    }

    (*regs)[TARGET_EF_R26] = 0;
    (*regs)[TARGET_EF_R27] = 0;
    (*regs)[TARGET_EF_LO] = tswapreg(env->active_tc.LO[0]);
    (*regs)[TARGET_EF_HI] = tswapreg(env->active_tc.HI[0]);
    (*regs)[TARGET_EF_CP0_EPC] = tswapreg(env->active_tc.PC);
    (*regs)[TARGET_EF_CP0_BADVADDR] = tswapreg(env->CP0_BadVAddr);
    (*regs)[TARGET_EF_CP0_STATUS] = tswapreg(env->CP0_Status);
    (*regs)[TARGET_EF_CP0_CAUSE] = tswapreg(env->CP0_Cause);
}
