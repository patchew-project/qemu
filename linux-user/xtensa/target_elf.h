/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef XTENSA_TARGET_ELF_H
#define XTENSA_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_XTENSA

/* See linux kernel: arch/xtensa/include/asm/elf.h.  */
#define ELF_NREG                128

#endif
