/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef AARCH64_TARGET_ELF_H
#define AARCH64_TARGET_ELF_H

#define ELF_ARCH                EM_AARCH64
#define ELF_CLASS               ELFCLASS64

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_HWCAP2         1
#define HAVE_ELF_PLATFORM       1

#define ELF_NREG                34

#if TARGET_BIG_ENDIAN
# define VDSO_HEADER            "vdso-be.c.inc"
#else
# define VDSO_HEADER            "vdso-le.c.inc"
#endif

#endif
