/*
 * Dummy cpu thread code
 *
 * Copyright IBM, Corp. 2011
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_DUMMY_CPUS_H
#define ACCEL_DUMMY_CPUS_H

void dummy_thread_precreate(CPUState *cpu);
void *dummy_cpu_thread_routine(void *arg);

#endif
