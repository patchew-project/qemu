/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_TARGET_ELF_H
#define LOONGARCH_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS64
#define ELF_MACHINE             EM_LOONGARCH
#define EXSTACK_DEFAULT         true
#define elf_check_arch(x)       ((x) == EM_LOONGARCH)
#define VDSO_HEADER             "vdso.c.inc"

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1

/* See linux kernel: arch/loongarch/include/asm/elf.h */
#define ELF_NREG                45

#endif
