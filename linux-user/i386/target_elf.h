/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef I386_TARGET_ELF_H
#define I386_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_386
#define EXSTACK_DEFAULT         true
#define VDSO_HEADER             "vdso.c.inc"

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1

/*
 * Note that ELF_NREG should be 19 as there should be place for
 * TRAPNO and ERR "registers" as well but linux doesn't dump those.
 *
 * See linux kernel: arch/x86/include/asm/elf.h
 */
#define ELF_NREG                17

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)       ((x) == EM_386 || (x) == EM_486)

/*
 * i386 is the only target which supplies AT_SYSINFO for the vdso.
 * All others only supply AT_SYSINFO_EHDR.
 */
#define DLINFO_ARCH_ITEMS (vdso_info != NULL)
#define ARCH_DLINFO                                     \
    do {                                                \
        if (vdso_info) {                                \
            NEW_AUX_ENT(AT_SYSINFO, vdso_info->entry);  \
        }                                               \
    } while (0)

#endif
