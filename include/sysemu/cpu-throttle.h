/*
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#ifndef SYSEMU_CPU_THROTTLE_H
#define SYSEMU_CPU_THROTTLE_H

#include "qemu/timer.h"

/**
 * cpu_throttle_init:
 *
 * Initialize the CPU throttling API.
 */
void cpu_throttle_init(void);

/**
 * cpu_throttle_set:
 * @new_throttle_pct: Percent of sleep time. Valid range is 1 to 99.
 *
 * Throttles all vcpus by forcing them to sleep for the given percentage of
 * time. A throttle_percentage of 25 corresponds to a 75% duty cycle roughly.
 * (example: 10ms sleep for every 30ms awake).
 *
 * cpu_throttle_set can be called as needed to adjust new_throttle_pct.
 * Once the throttling starts, it will remain in effect until cpu_throttle_stop
 * is called.
 */
void cpu_throttle_set(int new_throttle_pct);

/**
 * cpu_throttle_stop:
 *
 * Stops the vcpu throttling started by cpu_throttle_set.
 */
void cpu_throttle_stop(void);

/**
 * cpu_throttle_active:
 *
 * Returns: %true if the vcpus are currently being throttled, %false otherwise.
 */
bool cpu_throttle_active(void);

/**
 * cpu_throttle_get_percentage:
 *
 * Returns the vcpu throttle percentage. See cpu_throttle_set for details.
 *
 * Returns: The throttle percentage in range 1 to 99.
 */
int cpu_throttle_get_percentage(void);

/**
 * dirtylimit_enabled
 *
 * Returns: %true if dirty page limit for vCPU is enabled, %false otherwise.
 */
bool dirtylimit_enabled(int cpu_index);

/**
 * dirtylimit_is_vcpu_index_valid
 *
 * Returns: %true if cpu index valid, %false otherwise.
 */
bool dirtylimit_is_vcpu_index_valid(int cpu_index);

/**
 * dirtylimit_state_init:
 *
 * initialize golobal state for dirtylimit
 */
void dirtylimit_state_init(int max_cpus);

/**
 * dirtylimit_vcpu:
 *
 * impose dirtylimit on vcpu util reaching the quota dirtyrate
 */
void dirtylimit_vcpu(int cpu_index,
                     uint64_t quota);

/**
 * dirtylimit_query_vcpu:
 *
 * Returns: dirty page limit information of specified virtual CPU.
 */
struct DirtyLimitInfo *dirtylimit_query_vcpu(int cpu_index);

/**
 * dirtylimit_cancel_vcpu:
 *
 * cancel dirtylimit for the specified vcpu
 *
 * Returns: the number of running threads for dirtylimit
 */
int dirtylimit_cancel_vcpu(int cpu_index);

#endif /* SYSEMU_CPU_THROTTLE_H */
