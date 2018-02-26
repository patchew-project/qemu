/*
 * Copyright (c) 2018 Red Hat, Inc. and/or its affiliates
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef _TEST_MIGRATION_H_
#define _TEST_MIGRATION_H_

/* Common */
#define TEST_MEM_START      (1 * 1024 * 1024)
#define TEST_MEM_END        (100 * 1024 * 1024)
#define TEST_MEM_PAGE_SIZE  4096

/* PPC */
#define MIN_NVRAM_SIZE 8192 /* from spapr_nvram.c */

/* AArch64 */
#define ARM_MACH_VIRT_UART  0x09000000
/* AArch64 kernel load address is 0x40080000. It should be safe to start the
 * test memory at 0x40000000 + TEST_MEM_START, which literally creates a 512KB
 * space to host the kernel binary.
 */
#define ARM_TEST_MEM_START  (0x40000000 + TEST_MEM_START)
#define ARM_TEST_MEM_END    (0x40000000 + TEST_MEM_END)

#endif /* _TEST_MIGRATION_H_ */
