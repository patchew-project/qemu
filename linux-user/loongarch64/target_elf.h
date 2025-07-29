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
#define USE_ELF_CORE_DUMP

#endif
