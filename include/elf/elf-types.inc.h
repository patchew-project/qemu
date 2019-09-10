/*
 * Elf Type Specialisation
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * This code is licensed under the GNU .
 */

#ifndef _ELF_TYPES_INC_H_
#define _ELF_TYPES_INC_H_

#ifndef ELF_CLASS
#error you must define ELF_CLASS before including elf-types.inc.h
#else

#if ELF_CLASS == ELFCLASS32

#define elfhdr      elf32_hdr
#define elf_phdr    elf32_phdr
#define elf_note    elf32_note
#define elf_shdr    elf32_shdr
#define elf_sym     elf32_sym
#define elf_addr_t  Elf32_Off
#define elf_rela    elf32_rela

#ifdef ELF_USES_RELOCA
# define ELF_RELOC  Elf32_Rela
#else
# define ELF_RELOC  Elf32_Rel
#endif

#ifndef ElfW
#  define ElfW(x)   Elf32_ ## x
#  define ELFW(x)   ELF32_ ## x
#endif

#else /* ELF_CLASS == ELFCLASS64 */

#define elfhdr      elf64_hdr
#define elf_phdr    elf64_phdr
#define elf_note    elf64_note
#define elf_shdr    elf64_shdr
#define elf_sym     elf64_sym
#define elf_addr_t  Elf64_Off
#define elf_rela    elf64_rela

#ifdef ELF_USES_RELOCA
# define ELF_RELOC  Elf64_Rela
#else
# define ELF_RELOC  Elf64_Rel
#endif

#ifndef ElfW
#  define ElfW(x)   Elf64_ ## x
#  define ELFW(x)   ELF64_ ## x
#endif

#endif /* ELF_CLASS == ELFCLASS64 */
#endif /* ELF_CLASS */
#else
#error elf-types.inc.h should not be included twice in one compilation unit
#endif /* _ELF_TYPES_INC_H_ */
