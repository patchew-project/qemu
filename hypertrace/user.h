/*
 * QEMU-side management of hypertrace in user-level emulation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
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
 * @data_size: Output length in bytes for the data channel.
 *
 * Parse the commandline arguments for hypertrace.
 */
void hypertrace_opt_parse(const char *optarg, char **base, size_t *size);

/**
 * hypertrace_init:
 * @base: Base path for the hypertrace channel files.
 * @data_size: Length in bytes for the data channel.
 *
 * Initialize the backing files for the hypertrace channel.
 */
void hypertrace_init(const char *base, uint64_t data_size);

/**
 * hypertrace_guest_mmap:
 *
 * Check if this mmap is for the control channel and act accordingly.
 *
 * Precondition: defined(CONFIG_USER_ONLY)
 */
void hypertrace_guest_mmap(int fd, void *qemu_addr);

/**
 * hypertrace_fini:
 *
 * Remove the backing files for the hypertrace channel.
 */
void hypertrace_fini(void);
