/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_CPUS_H
#define TCG_CPUS_H

#include "sysemu/cpus.h"

#include "tcg-cpus-rr.h"
#include "tcg-cpus-icount.h"
#include "tcg-cpus-mttcg.h"

void tcg_start_vcpu_thread(CPUState *cpu);
void qemu_tcg_destroy_vcpu(CPUState *cpu);
int tcg_cpu_exec(CPUState *cpu);
void tcg_handle_interrupt(CPUState *cpu, int mask);

#endif /* TCG_CPUS_H */
