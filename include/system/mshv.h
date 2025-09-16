/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou  <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Jinank Jain  <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_INT_H
#define QEMU_MSHV_INT_H

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "hw/hyperv/hyperv-proto.h"
#include "linux/mshv.h"
#include "hw/hyperv/hvhdk.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "qapi/qapi-types-common.h"
#include "system/memory.h"
#include "accel/accel-ops.h"

#ifdef COMPILING_PER_TARGET
#ifdef CONFIG_MSHV
#define CONFIG_MSHV_IS_POSSIBLE
#endif
#else
#define CONFIG_MSHV_IS_POSSIBLE
#endif

typedef struct hyperv_message hv_message;

#define MSHV_MAX_MSI_ROUTES 4096

#define MSHV_PAGE_SHIFT 12

#ifdef CONFIG_MSHV_IS_POSSIBLE
extern bool mshv_allowed;
#define mshv_enabled() (mshv_allowed)

typedef struct MshvMemoryListener {
    MemoryListener listener;
    int as_id;
} MshvMemoryListener;

typedef struct MshvAddressSpace {
    MshvMemoryListener *ml;
    AddressSpace *as;
} MshvAddressSpace;

typedef struct MshvState {
    AccelState parent_obj;
    int vm;
    MshvMemoryListener memory_listener;
    /* number of listeners */
    int nr_as;
    MshvAddressSpace *as;
    int fd;
} MshvState;
extern MshvState *mshv_state;

struct AccelCPUState {
    int cpufd;
    bool dirty;
};

typedef struct MshvMsiControl {
    bool updated;
    GHashTable *gsi_routes;
} MshvMsiControl;

#define mshv_vcpufd(cpu) (cpu->accel->cpufd)

#else /* CONFIG_MSHV_IS_POSSIBLE */
#define mshv_enabled() false
#endif
#define mshv_msi_via_irqfd_enabled() mshv_enabled()

/* cpu */
typedef struct MshvFPU {
    uint8_t fpr[8][16];
    uint16_t fcw;
    uint16_t fsw;
    uint8_t ftwx;
    uint8_t pad1;
    uint16_t last_opcode;
    uint64_t last_ip;
    uint64_t last_dp;
    uint8_t xmm[16][16];
    uint32_t mxcsr;
    uint32_t pad2;
} MshvFPU;

typedef enum MshvVmExit {
    MshvVmExitIgnore   = 0,
    MshvVmExitShutdown = 1,
    MshvVmExitSpecial  = 2,
} MshvVmExit;

void mshv_init_mmio_emu(void);
int mshv_create_vcpu(int vm_fd, uint8_t vp_index, int *cpu_fd);
void mshv_remove_vcpu(int vm_fd, int cpu_fd);
int mshv_configure_vcpu(const CPUState *cpu, const MshvFPU *fpu, uint64_t xcr0);
int mshv_get_standard_regs(CPUState *cpu);
int mshv_get_special_regs(CPUState *cpu);
int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit);
int mshv_load_regs(CPUState *cpu);
int mshv_store_regs(CPUState *cpu);
int mshv_set_generic_regs(const CPUState *cpu, const hv_register_assoc *assocs,
                          size_t n_regs);
int mshv_arch_put_registers(const CPUState *cpu);
void mshv_arch_init_vcpu(CPUState *cpu);
void mshv_arch_destroy_vcpu(CPUState *cpu);
void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features);
int mshv_arch_post_init_vm(int vm_fd);

int mshv_hvcall(int fd, const struct mshv_root_hvcall *args);

/* memory */
typedef struct MshvMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    bool readonly;
} MshvMemoryRegion;

int mshv_add_mem(int vm_fd, const MshvMemoryRegion *mr);
int mshv_remove_mem(int vm_fd, const MshvMemoryRegion *mr);
void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add);

/* interrupt */
void mshv_init_msicontrol(void);
int mshv_request_interrupt(int vm_fd, uint32_t interrupt_type, uint32_t vector,
                           uint32_t vp_index, bool logical_destination_mode,
                           bool level_triggered);

int mshv_irqchip_add_msi_route(int vector, PCIDevice *dev);
int mshv_irqchip_update_msi_route(int virq, MSIMessage msg, PCIDevice *dev);
void mshv_irqchip_commit_routes(void);
void mshv_irqchip_release_virq(int virq);
int mshv_irqchip_add_irqfd_notifier_gsi(const EventNotifier *n,
                                        const EventNotifier *rn, int virq);
int mshv_irqchip_remove_irqfd_notifier_gsi(const EventNotifier *n, int virq);
int mshv_reserve_ioapic_msi_routes(int vm_fd);

#endif
