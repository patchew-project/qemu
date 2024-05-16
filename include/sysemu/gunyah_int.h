/*
 * QEMU Gunyah hypervisor support
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* header to be included in Gunyah-specific code */

#ifndef GUNYAH_INT_H
#define GUNYAH_INT_H

#include "qemu/accel.h"
#include "qemu/typedefs.h"
#include "qemu/thread.h"

typedef struct gunyah_slot {
    uint64_t start;
    uint64_t size;
    uint8_t *mem;
    uint32_t id;
    uint32_t flags;

    /*
     * @lend indicates if memory was lent.
     *
     * This flag is temporarily used until the upstream Gunyah kernel driver
     * patches are updated to support indication of lend vs share via flags
     * field of GH_SET_USER_MEM_API interface.
     */
    bool lend;
} gunyah_slot;

#define GUNYAH_MAX_MEM_SLOTS    32

struct GUNYAHState {
    AccelState parent_obj;

    QemuMutex slots_lock;
    gunyah_slot slots[GUNYAH_MAX_MEM_SLOTS];
    uint32_t nr_slots;
    int fd;
    int vmfd;
    uint64_t swiotlb_size;
    bool preshmem_reserved;
    uint32_t preshmem_size;
    uint32_t nr_irqs;
    uint32_t vm_started;
};

struct AccelCPUState {
    int fd;
    struct gh_vcpu_run *run;
};

int gunyah_create_vm(void);
void gunyah_start_vm(void);
int gunyah_vm_ioctl(int type, ...);
void *gunyah_cpu_thread_fn(void *arg);
int gunyah_add_irqfd(int irqfd, int label, Error **errp);
GUNYAHState *get_gunyah_state(void);
int gunyah_arch_put_registers(CPUState *cs, int level);
void gunyah_cpu_synchronize_post_reset(CPUState *cpu);

#endif    /* GUNYAH_INT_H */
