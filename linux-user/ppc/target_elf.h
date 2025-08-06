/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef PPC_TARGET_ELF_H
#define PPC_TARGET_ELF_H

#define ELF_MACHINE             PPC_ELF_MACHINE

#ifdef TARGET_PPC64
# define elf_check_arch(x)      ((x) == EM_PPC64)
# define ELF_CLASS              ELFCLASS64
#else
# define ELF_CLASS              ELFCLASS32
# define EXSTACK_DEFAULT        true
#endif
#define ELF_ARCH                EM_PPC

/*
 * The requirements here are:
 * - keep the final alignment of sp (sp & 0xf)
 * - make sure the 32-bit value at the first 16 byte aligned position of
 *   AUXV is greater than 16 for glibc compatibility.
 *   AT_IGNOREPPC is used for that.
 * - for compatibility with glibc ARCH_DLINFO must always be defined on PPC,
 *   even if DLINFO_ARCH_ITEMS goes to zero or is undefined.
 */
#define DLINFO_ARCH_ITEMS       5
#define ARCH_DLINFO                                     \
    do {                                                \
        PowerPCCPU *cpu = POWERPC_CPU(thread_cpu);              \
        /*                                              \
         * Handle glibc compatibility: these magic entries must \
         * be at the lowest addresses in the final auxv.        \
         */                                             \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);        \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);        \
        NEW_AUX_ENT(AT_DCACHEBSIZE, cpu->env.dcache_line_size); \
        NEW_AUX_ENT(AT_ICACHEBSIZE, cpu->env.icache_line_size); \
        NEW_AUX_ENT(AT_UCACHEBSIZE, 0);                 \
    } while (0)

#define USE_ELF_CORE_DUMP

#ifndef TARGET_PPC64
# define VDSO_HEADER  "vdso-32.c.inc"
#elif TARGET_BIG_ENDIAN
# define VDSO_HEADER  "vdso-64.c.inc"
#else
# define VDSO_HEADER  "vdso-64le.c.inc"
#endif

#endif
