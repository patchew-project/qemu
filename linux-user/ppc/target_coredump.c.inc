/* SPDX-License-Identifier: GPL-2.0-or-later */

/* See linux kernel: arch/powerpc/include/asm/elf.h.  */
#define ELF_NREG 48
typedef target_elf_greg_t target_elf_gregset_t[ELF_NREG];

static void elf_core_copy_regs(target_elf_gregset_t *regs,
                               const CPUPPCState *env)
{
    target_ulong ccr = 0;

    for (int i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        (*regs)[i] = tswapreg(env->gpr[i]);
    }

    (*regs)[32] = tswapreg(env->nip);
    (*regs)[33] = tswapreg(env->msr);
    (*regs)[35] = tswapreg(env->ctr);
    (*regs)[36] = tswapreg(env->lr);
    (*regs)[37] = tswapreg(cpu_read_xer(env));

    ccr = ppc_get_cr(env);
    (*regs)[38] = tswapreg(ccr);
}
