/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef X86_64_TARGET_ELF_H
#define X86_64_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS64
#define ELF_ARCH                EM_X86_64
#define VDSO_HEADER             "vdso.c.inc"
#define USE_ELF_CORE_DUMP

#endif
