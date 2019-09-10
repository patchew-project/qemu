/*
 * Elf Architecture Definition
 *
 * This is a simple expansion to define common Elf types for the
 * various machines for the various places it's needed in the source
 * tree.
 *
 * Copyright (c) 2019 Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef _ELF_ARCH_H_
#define _ELF_ARCH_H_

#include "elf/elf.h"

#ifndef NEED_CPU_H
#error Needs an target definition
#endif

#ifdef ELF_ARCH
#error ELF_ARCH should only be defined once in this file
#endif

#ifdef TARGET_I386
#ifdef TARGET_X86_64
#define ELF_ARCH EM_X86_64
#else
#define ELF_ARCH EM_386
#endif
#endif

#ifdef TARGET_ARM
#ifndef TARGET_AARCH64
#define ELF_ARCH EM_ARM
#else
#define ELF_ARCH EM_AARCH64
#endif
#endif

#ifdef TARGET_SPARC
#ifdef TARGET_SPARC64
#define ELF_ARCH EM_SPARCV9
#else
#define ELF_ARCH EM_SPARC
#endif
#endif

#ifdef TARGET_PPC
#define ELF_ARCH EM_PPC
#endif

#ifdef TARGET_MIPS
#define ELF_ARCH EM_MIPS
#endif

#ifdef TARGET_MICROBLAZE
#define ELF_ARCH EM_MICROBLAZE
#endif

#ifdef TARGET_NIOS2
#define ELF_ARCH EM_ALTERA_NIOS2
#endif

#ifdef TARGET_OPENRISC
#define ELF_ARCH EM_OPENRISC
#endif

#ifdef TARGET_SH4
#define ELF_ARCH EM_SH
#endif

#ifdef TARGET_CRIS
#define ELF_ARCH EM_CRIS
#endif

#ifdef TARGET_M68K
#define ELF_ARCH EM_68K
#endif

#ifdef TARGET_ALPHA
#define ELF_ARCH EM_ALPHA
#endif

#ifdef TARGET_S390X
#define ELF_ARCH EM_S390
#endif

#ifdef TARGET_TILEGX
#define ELF_ARCH EM_TILEGX
#endif

#ifdef TARGET_RISCV
#define ELF_ARCH EM_RISCV
#endif

#ifdef TARGET_HPPA
#define ELF_ARCH EM_PARISC
#endif

#ifdef TARGET_XTENSA
#define ELF_ARCH EM_XTENSA
#endif

#endif /* _ELF_ARCH_H_ */
