/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MICROBLAZE_TARGET_ELF_H
#define MICROBLAZE_TARGET_ELF_H

#define ELF_CLASS           ELFCLASS32
#define ELF_MACHINE         EM_MICROBLAZE
#define elf_check_machine(x) ((x) == EM_MICROBLAZE || (x) == EM_MICROBLAZE_OLD)
#define USE_ELF_CORE_DUMP

#endif
