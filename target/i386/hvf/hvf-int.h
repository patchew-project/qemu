/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * Copyright Google Inc., 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in HVF-specific internal code */

#ifndef HVF_INT_H
#define HVF_INT_H

int hvf_init_vcpu(CPUState *);
int hvf_vcpu_exec(CPUState *);
void hvf_cpu_synchronize_state(CPUState *);
void hvf_cpu_synchronize_post_reset(CPUState *);
void hvf_cpu_synchronize_post_init(CPUState *);
void hvf_cpu_synchronize_pre_loadvm(CPUState *);
void hvf_vcpu_destroy(CPUState *);

#endif /* HVF_INT_H */
