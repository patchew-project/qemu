/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdint.h>
#include <sys/types.h>


/**
 * Definition of QEMU options describing hypertrace subsystem configuration
 */
extern QemuOptsList qemu_hypertrace_opts;

/**
 * hypertrace_opt_parse:
 * @optarg: Input arguments.
 * @base: Output base path for the hypertrace channel files.
 * @max_clients: Output maximum number of concurrent clients.
 *
 * Parse the commandline arguments for hypertrace.
 */
void hypertrace_opt_parse(const char *optarg, char **base,
                          unsigned int *max_clients);

/**
 * hypertrace_init:
 * @base: Base path for the hypertrace channel files.
 * @max_clients: Maximum number of concurrent clients.
 *
 * Initialize the backing files for the hypertrace channel.
 */
void hypertrace_init(const char *base, unsigned int max_clients);

/**
 * hypertrace_guest_mmap_check:
 *
 * Check whether the mapped file is *not* hypertrace's control channel; if it
 * is, check it is mapped correctly.
 *
 * Precondition: defined(CONFIG_USER_ONLY)
 */
bool hypertrace_guest_mmap_check(int fd, unsigned long len,
                                 unsigned long offset);

/**
 * hypertrace_guest_mmap_apply:
 *
 * Configure initial mprotect if mapping the control channel.
 *
 * Precondition: defined(CONFIG_USER_ONLY)
 */
void hypertrace_guest_mmap_apply(int fd, void *qemu_addr, CPUState *vcpu);

/**
 * hypertrace_fini:
 *
 * Remove the backing files for the hypertrace channel.
 */
void hypertrace_fini(void);


/* Internal signal management */
extern bool sigint_user_set;
extern struct sigaction sigint_user;
extern bool sigabrt_user_set;
extern struct sigaction sigabrt_user;
