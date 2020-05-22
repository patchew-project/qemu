#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

#include "qemu/timer.h"

/* cpus.c */

/* CPU execution threads */

typedef struct CpusAccelInterface {
    void (*create_vcpu_thread)(CPUState *cpu);
    void (*kick_vcpu_thread)(CPUState *cpu);

    void (*cpu_synchronize_post_reset)(CPUState *cpu);
    void (*cpu_synchronize_post_init)(CPUState *cpu);
    void (*cpu_synchronize_state)(CPUState *cpu);
    void (*cpu_synchronize_pre_loadvm)(CPUState *cpu);
} CpusAccelInterface;

/* register accel-specific interface */
void cpus_register_accel_interface(CpusAccelInterface *i);

/*
 * these are the registrable vcpu start functions for all accelerators.
 * They are arguments to qemu_register_start_vcpu();
 */
void qemu_dummy_start_vcpu(CPUState *cpu);
void qemu_tcg_init_vcpu(CPUState *cpu);
void qemu_kvm_start_vcpu(CPUState *cpu);
void qemu_hax_start_vcpu(CPUState *cpu);
void qemu_hvf_start_vcpu(CPUState *cpu);
void qemu_whpx_start_vcpu(CPUState *cpu);
/* end of vcpu start functions for accelerators */

/* interface available for cpus accelerator threads */

/* For temporary buffers for forming a name */
#define VCPU_THREAD_NAME_SIZE 16

void cpus_kick_thread(CPUState *cpu);
bool cpu_thread_is_idle(CPUState *cpu);
bool all_cpu_threads_idle(void);
bool cpu_can_run(CPUState *cpu);
void qemu_wait_io_event_common(CPUState *cpu);
void qemu_wait_io_event(CPUState *cpu);
void cpu_thread_signal_created(CPUState *cpu);
void cpu_thread_signal_destroyed(CPUState *cpu);
void cpu_handle_guest_debug(CPUState *cpu);

/* end interface for cpus accelerator threads */

bool qemu_in_vcpu_thread(void);
void qemu_init_cpu_loop(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);

extern int icount_align_option;

/* Unblock cpu */
void qemu_cpu_kick_self(void);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);
void cpu_synchronize_all_pre_loadvm(void);

#ifndef CONFIG_USER_ONLY
/* vl.c */
/* *-user doesn't have configurable SMP topology */
extern int smp_cores;
extern int smp_threads;
#endif

void list_cpus(const char *optarg);

#endif
