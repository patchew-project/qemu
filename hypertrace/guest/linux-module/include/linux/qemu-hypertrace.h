/* -*- C -*-
 *
 * Guest-side management of hypertrace.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#ifndef QEMU_HYPERTRACE_H
#define QEMU_HYPERTRACE_H

#include <linux/types.h>


/**
 * qemu_hypertrace_num_args:
 *
 * Number of uint64_t values read by each call to qemu_hypertrace().
 */
static uint64_t qemu_hypertrace_num_args(void);

/**
 * qemu_hypertrace_data_size:
 *
 * Maximum data offset value accepted by other calls.
 */
static uint64_t qemu_hypertrace_max_offset(void);

/**
 * qemu_hypertrace_data:
 * @data_offset: Offset in multiples of argument packs.
 *
 * Pointer to the start of the data channel.
 */
static uint64_t *qemu_hypertrace_data(uint64_t data_offset);

/**
 * qemu_hypertrace:
 * @data_offset: Offset in multiples of argument packs.
 *
 * Invoke the control channel.
 *
 * Each of the users (e.g., CPU) of the hypertrace channel can use a different
 * data offset to ensure they can work concurrently without using locks (i.e.,
 * each uses a different portion of the data channel).
 *
 * Note: You should use wmb() before writing into the control channel iff you
 * have written into the data channel.
 *
 * Note: Use preempt_disable() and preempt_enable() if you're using data offsets
 * based on the CPU identifiers (or else data might be mixed if a task is
 * re-scheduled).
 */
static void qemu_hypertrace(uint64_t data_offset);


#include <linux/qemu-hypertrace-internal.h>

#endif  /* QEMU_HYPERTRACE_H */
