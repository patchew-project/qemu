/*
 * Guest-side management of hypertrace.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdint.h>
#include <sys/types.h>


/**
 * qemu_hypertrace_init:
 * @base: Base path to the hypertrace channel.
 *
 * Initialize the hypertrace channel.
 *
 * The base path to the hypertrace channel depends on the type of QEMU target:
 *
 * - User (single-application)
 *   The base path provided when starting QEMU ("-hypertrace" commandline
 *   option).
 *
 * - System (OS-dependant)
 *   + Linux
 *     The base path to the hypertrace channel virtual device; on a default QEMU
 *     device setup for x86 this is "/sys/devices/pci0000:00/0000:00:04.0". If
 *     NULL is provided, the hypertrace device will be automatically detected.
 *
 * Returns: Zero on success.
 */
int qemu_hypertrace_init(const char *base);

/**
 * qemu_hypertrace_fini:
 *
 * Deinitialize the hypertrace channel.
 *
 * Returns: Zero on success.
 */
int qemu_hypertrace_fini(void);

/**
 * qemu_hypertrace_num_args:
 *
 * Number of uint64_t values read by each call to qemu_hypertrace().
 */
uint64_t qemu_hypertrace_num_args(void);

/**
 * qemu_hypertrace_data_size:
 *
 * Maximum data offset value accepted by other calls.
 */
uint64_t qemu_hypertrace_max_offset(void);

/**
 * qemu_hypertrace_data:
 * @data_offset: Offset in multiples of argument packs.
 *
 * Pointer to the start of the data channel.
 */
uint64_t *qemu_hypertrace_data(uint64_t data_offset);

/**
 * qemu_hypertrace:
 * @data_offset: Offset in multiples of argument packs.
 *
 * Invoke the control channel.
 *
 * Each of the users (e.g., thread) of the hypertrace channel can use a
 * different data offset to ensure they can work concurrently without using
 * locks (i.e., each uses a different portion of the data channel).
 */
void qemu_hypertrace(uint64_t data_offset);
