/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYSTEM_WHPX_ALL_H
#define SYSTEM_WHPX_ALL_H

/* Called by whpx-common */
int whpx_vcpu_run(CPUState *cpu);
void whpx_get_registers(CPUState *cpu);
void whpx_set_registers(CPUState *cpu, int level);
int whpx_accel_init(AccelState *as, MachineState *ms);
void whpx_cpu_instance_init(CPUState *cs);
HRESULT whpx_set_exception_exit_bitmap(UINT64 exceptions);
#endif
