#ifndef SYSTEM_WHPX_ALL_H
#define SYSTEM_WHPX_ALL_H

/* Called by whpx-common */
int whpx_vcpu_run(CPUState *cpu);
void whpx_get_registers(CPUState *cpu);
void whpx_set_registers(CPUState *cpu, int level);
int whpx_accel_init(AccelState *as, MachineState *ms);
#ifdef __x86_64__
void whpx_cpu_instance_init(CPUState *cs);
#endif
#endif
