/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MIPS64_TARGET_ELF_H
#define MIPS64_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS64
#define ELF_ARCH                EM_MIPS
#define EXSTACK_DEFAULT         true

#ifdef TARGET_ABI_MIPSN32
#define elf_check_abi(x)        ((x) & EF_MIPS_ABI2)
#else
#define elf_check_abi(x)        (!((x) & EF_MIPS_ABI2))
#endif

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_BASE_PLATFORM  1

/* See linux kernel: arch/mips/include/asm/elf.h.  */
#define ELF_NREG                45

#endif
