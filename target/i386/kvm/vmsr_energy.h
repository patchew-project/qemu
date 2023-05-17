/*
 * QEMU KVM support -- x86 virtual energy-related MSR.
 *
 * Copyright 2023 Red Hat, Inc. 2023
 *
 *  Author:
 *      Anthony Harivel <aharivel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VMSR_ENERGY_H
#define VMSR_ENERGY_H

#include "qemu/osdep.h"

#include <numa.h>

/*
 * Define the interval time in micro seconds between 2 samples of
 * energy related MSRs
 */
#define MSR_ENERGY_THREAD_SLEEP_US 1000000.0

/*
 * Thread statistic
 * @ thread_id: TID (thread ID)
 * @ is_vcpu: true is thread is vCPU thread
 * @ cpu_id: CPU number last executed on
 * @ vcpu_id: vCPU ID
 * @ numa_node_id:node number of the CPU
 * @ utime: amount of clock ticks the thread
 *          has been scheduled in User mode
 * @ stime: amount of clock ticks the thread
 *          has been scheduled in System mode
 * @ delta_ticks: delta of utime+stime between
 *          the two samples (before/after sleep)
 */
struct thread_stat {
    unsigned int thread_id;
    bool is_vcpu;
    unsigned int cpu_id;
    unsigned int vcpu_id;
    unsigned int numa_node_id;
    unsigned long long *utime;
    unsigned long long *stime;
    unsigned long long delta_ticks;
};

/*
 * Package statistic
 * @ e_start: package energy counter before the sleep
 * @ e_end: package energy counter after the sleep
 * @ e_delta: delta of package energy counter
 * @ e_ratio: store the energy ratio of non-vCPU thread
 * @ nb_vcpu: number of vCPU running on this package
 */
struct packge_energy_stat {
    uint64_t e_start;
    uint64_t e_end;
    uint64_t e_delta;
    uint64_t e_ratio;
    unsigned int nb_vcpu;
};

typedef struct thread_stat thread_stat;
typedef struct packge_energy_stat package_energy_stat;

uint64_t read_msr(uint32_t reg, unsigned int cpu_id);
void delta_ticks(thread_stat *thd_stat, int i);
unsigned int get_maxcpus(unsigned int package_num);
int read_thread_stat(struct thread_stat *thread, int pid, int index);
pid_t *get_thread_ids(pid_t pid, int *num_threads);
double get_ratio(package_energy_stat *pkg_stat,
                        thread_stat *thd_stat,
                        int maxticks, int i);

#endif /* VMSR_ENERGY_H */
