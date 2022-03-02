/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in HVF-specific code */

#ifndef HVF_INT_H
#define HVF_INT_H

#ifdef __aarch64__
#include <Hypervisor/Hypervisor.h>
#else
#include <Hypervisor/hv.h>
#endif

typedef struct hvf_vcpu_caps {
    uint64_t vmx_cap_pinbased;
    uint64_t vmx_cap_procbased;
    uint64_t vmx_cap_procbased2;
    uint64_t vmx_cap_entry;
    uint64_t vmx_cap_exit;
    uint64_t vmx_cap_preemption_timer;
} hvf_vcpu_caps;

struct HVFState {
    AccelState parent;

    hvf_vcpu_caps *hvf_caps;
    uint64_t vtimer_offset;
};
extern HVFState *hvf_state;

struct hvf_vcpu_state {
    uint64_t fd;
    void *exit;
    bool vtimer_masked;
    sigset_t unblock_ipi_mask;
};

void assert_hvf_ok(hv_return_t ret);
int hvf_arch_init(void);
int hvf_arch_init_vcpu(CPUState *cpu);
void hvf_arch_vcpu_destroy(CPUState *cpu);
int hvf_vcpu_exec(CPUState *);
int hvf_put_registers(CPUState *);
int hvf_get_registers(CPUState *);
void hvf_kick_vcpu_thread(CPUState *cpu);

bool hvf_access_memory(hwaddr address, bool write);
void hvf_init_memslots(void);

#endif
