/*
 * QEMU target-specific directory macros
 *
 * Copyright (C) 2018 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_TARGET_DIR_H
#define QEMU_TARGET_DIR_H

#if defined(TARGET_ALPHA)
#define TARGET_DIR_PREFIX ../target/alpha
#elif defined(TARGET_ARM)
#define TARGET_DIR_PREFIX ../target/arm
#elif defined(TARGET_CRIS)
#define TARGET_DIR_PREFIX ../target/cris
#elif defined(TARGET_HPPA)
#define TARGET_DIR_PREFIX ../target/hppa
#elif defined(TARGET_I386)
#define TARGET_DIR_PREFIX ../target/i386
#elif defined(TARGET_LM32)
#define TARGET_DIR_PREFIX ../target/lm32
#elif defined(TARGET_M68K)
#define TARGET_DIR_PREFIX ../target/m68k
#elif defined(TARGET_MICROBLAZE)
#define TARGET_DIR_PREFIX ../target/microblaze
#elif defined(TARGET_MIPS)
#define TARGET_DIR_PREFIX ../target/mips
#elif defined(TARGET_MOXIE)
#define TARGET_DIR_PREFIX ../target/moxie
#elif defined(TARGET_NIOS2)
#define TARGET_DIR_PREFIX ../target/nios2
#elif defined(TARGET_OPENRISC)
#define TARGET_DIR_PREFIX ../target/openrisc
#elif defined(TARGET_PPC)
#define TARGET_DIR_PREFIX ../target/ppc
#elif defined(TARGET_RISCV)
#define TARGET_DIR_PREFIX ../target/riscv
#elif defined(TARGET_S390X)
#define TARGET_DIR_PREFIX ../target/s390x
#elif defined(TARGET_SH4)
#define TARGET_DIR_PREFIX ../target/sh4
#elif defined(TARGET_SPARC)
#define TARGET_DIR_PREFIX ../target/sparc
#elif defined(TARGET_UNICORE32)
#define TARGET_DIR_PREFIX ../target/unicore32
#elif defined(TARGET_TILEGX)
#define TARGET_DIR_PREFIX ../target/tilegx
#elif defined(TARGET_TRICORE)
#define TARGET_DIR_PREFIX ../target/tricore
#elif defined(TARGET_XTENSA)
#define TARGET_DIR_PREFIX ../target/xtensa
#else
#error "Target-specific directory include missing"
#endif

#define TARGET_DIR_HASH(file) #file
#define TARGET_DIR_STRING(file) TARGET_DIR_HASH(file)
#define TARGET_DIR(file) TARGET_DIR_STRING(TARGET_DIR_PREFIX/file)

#endif
