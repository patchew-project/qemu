/*
 * Copyright (c) 2018 Red Hat, Inc. and/or its affiliates
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BOOTFILE_H
#define BOOTFILE_H

/*
 * This file is included from qtest, but also from the assembly code
 * that generates the header file containing the guest workload. Don't
 * expose the function declarations and non-standard types to it.
 */
#ifdef __ASSEMBLER__
#define MIGRATION_GUEST_CODE
#else
#include "libqtest.h"
#endif

/* Common */
#define TEST_MEM_PAGE_SIZE 4096

/*
 * An initial 3 MB offset is used as that corresponds
 * to ~1 sec of data transfer with our bandwidth setting.
 */
#define MAGIC_OFFSET_BASE (3 * 1024 * 1024)
/*
 * A further 1k is added to ensure we're not a multiple
 * of TEST_MEM_PAGE_SIZE, thus avoid clash with writes
 * from the migration guest workload.
 */
#define MAGIC_OFFSET_SHUFFLE 1024
#define MAGIC_OFFSET (MAGIC_OFFSET_BASE + MAGIC_OFFSET_SHUFFLE)
#define MAGIC_MARKER 0xFEED12345678CAFEULL

/* x86 */
#define X86_TEST_MEM_START (1 * 1024 * 1024)
#define X86_TEST_MEM_END   (100 * 1024 * 1024)

/* S390 */
#define S390_TEST_MEM_START (1 * 1024 * 1024)
#define S390_TEST_MEM_END   (100 * 1024 * 1024)

/* PPC */
#define PPC_TEST_MEM_START (1 * 1024 * 1024)
#define PPC_TEST_MEM_END   (100 * 1024 * 1024)
#define PPC_H_PUT_TERM_CHAR 0x58

/* ARM */
#define ARM_TEST_MEM_START (0x40000000 + 1 * 1024 * 1024)
#define ARM_TEST_MEM_END   (0x40000000 + 100 * 1024 * 1024)
#define ARM_MACH_VIRT_UART 0x09000000
/* AArch64 kernel load address is 0x40080000, and the test memory starts at
 * 0x40100000. So the maximum allowable kernel size is 512KB.
 */
#define ARM_TEST_MAX_KERNEL_SIZE (512 * 1024)

/* LoongArch64 */
#define LOONGARCH_TEST_MEM_START (32 * 1024 * 1024)
#define LOONGARCH_TEST_MEM_END   (100 * 1024 * 1024)

#ifndef MIGRATION_GUEST_CODE
void bootfile_delete(void);
char *bootfile_create(const char *arch, const char *dir, bool suspend_me);
char *bootfile_get(void);
char *bootfile_get_opts(const char *arch, char **memory_size);
void migrate_prepare_for_dirty_mem(QTestState *from);
void migrate_wait_for_dirty_mem(QTestState *from, QTestState *to);
void migrate_check_guests_ram(QTestState *who);
#endif

#endif /* BOOTFILE_H */
