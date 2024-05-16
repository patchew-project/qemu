/*
 * QEMU Gunyah hypervisor support
 *
 * (based on KVM accelerator code structure)
 *
 * Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "hw/core/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/gunyah.h"
#include "sysemu/gunyah_int.h"
#include "linux-headers/linux/gunyah.h"
#include "exec/memory.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qemu/event_notifier.h"
#include "qemu/main-loop.h"
#include "sysemu/runstate.h"
#include "qemu/guest-random.h"

static void gunyah_region_add(MemoryListener *listener,
                           MemoryRegionSection *section);
static void gunyah_region_del(MemoryListener *listener,
                           MemoryRegionSection *section);
static void gunyah_mem_ioeventfd_add(MemoryListener *listener,
                                  MemoryRegionSection *section,
                                  bool match_data, uint64_t data,
                                  EventNotifier *e);
static void gunyah_mem_ioeventfd_del(MemoryListener *listener,
                                  MemoryRegionSection *section,
                                  bool match_data, uint64_t data,
                                  EventNotifier *e);

/* Keep this here until Linux kernel UAPI header file (gunyah.h) is updated */
enum gh_vm_exit_type {
    GH_RM_EXIT_TYPE_VM_EXIT = 0,
    GH_RM_EXIT_TYPE_PSCI_POWER_OFF = 1,
    GH_RM_EXIT_TYPE_PSCI_SYSTEM_RESET = 2,
    GH_RM_EXIT_TYPE_PSCI_SYSTEM_RESET2 = 3,
    GH_RM_EXIT_TYPE_WDT_BITE = 4,
    GH_RM_EXIT_TYPE_HYP_ERROR = 5,
    GH_RM_EXIT_TYPE_ASYNC_EXT_ABORT = 6,
    GH_RM_EXIT_TYPE_VM_FORCE_STOPPED = 7,
};

static int gunyah_ioctl(int type, ...)
{
    void *arg;
    va_list ap;
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    assert(s->fd);

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    return ioctl(s->fd, type, arg);
}

int gunyah_vm_ioctl(int type, ...)
{
    void *arg;
    va_list ap;
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    assert(s->vmfd);

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    return ioctl(s->vmfd, type, arg);
}

static int gunyah_vcpu_ioctl(CPUState *cpu, int type, ...)
{
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    return ioctl(cpu->accel->fd, type, arg);
}

static MemoryListener gunyah_memory_listener = {
    .name = "gunyah",
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
    .region_add = gunyah_region_add,
    .region_del = gunyah_region_del,
    .eventfd_add = gunyah_mem_ioeventfd_add,
    .eventfd_del = gunyah_mem_ioeventfd_del,
};

int gunyah_create_vm(void)
{
    GUNYAHState *s;
    int i;

    s = GUNYAH_STATE(current_accel());

    s->fd = qemu_open_old("/dev/gunyah", O_RDWR);
    if (s->fd == -1) {
        error_report("Could not access Gunyah kernel module at /dev/gunyah: %s",
                                strerror(errno));
        exit(1);
    }

    s->vmfd = gunyah_ioctl(GH_CREATE_VM, 0);
    if (s->vmfd < 0) {
        error_report("Could not create VM: %s", strerror(errno));
        exit(1);
    }

    qemu_mutex_init(&s->slots_lock);
    s->nr_slots = GUNYAH_MAX_MEM_SLOTS;
    for (i = 0; i < s->nr_slots; ++i) {
        s->slots[i].start = 0;
        s->slots[i].size = 0;
        s->slots[i].id = i;
    }

    memory_listener_register(&gunyah_memory_listener, &address_space_memory);
    return 0;
}

#define gunyah_slots_lock(s)    qemu_mutex_lock(&s->slots_lock)
#define gunyah_slots_unlock(s)  qemu_mutex_unlock(&s->slots_lock)

static gunyah_slot *gunyah_find_overlap_slot(GUNYAHState *s,
                uint64_t start, uint64_t size)
{
    gunyah_slot *slot;
    int i;

    for (i = 0; i < s->nr_slots; ++i) {
        slot = &s->slots[i];
        if (slot->size && start < (slot->start + slot->size) &&
            (start + size) > slot->start) {
            return slot;
        }
    }

    return NULL;
}

/* Called with s->slots_lock held */
static gunyah_slot *gunyah_get_free_slot(GUNYAHState *s)
{
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        if (s->slots[i].size == 0) {
            return &s->slots[i];
        }
    }

    return NULL;
}

static void gunyah_add_mem(GUNYAHState *s, MemoryRegionSection *section,
        bool lend, enum gh_mem_flags flags)
{
    gunyah_slot *slot;
    MemoryRegion *area = section->mr;
    struct gh_userspace_memory_region gumr;
    int ret;

    slot = gunyah_get_free_slot(s);
    if (!slot) {
        error_report("No free slots to add memory!");
        exit(1);
    }

    slot->size = int128_get64(section->size);
    slot->mem = memory_region_get_ram_ptr(area) + section->offset_within_region;
    slot->start = section->offset_within_address_space;
    slot->lend = lend;

    gumr.label = slot->id;
    gumr.flags = flags;
    gumr.guest_phys_addr = slot->start;
    gumr.memory_size = slot->size;
    gumr.userspace_addr = (__u64) slot->mem;

    /*
     * GH_VM_ANDROID_LEND_USER_MEM is temporary, until
     * GH_VM_SET_USER_MEM_REGION is enhanced to support lend option also.
     */
    if (lend) {
        ret = gunyah_vm_ioctl(GH_VM_ANDROID_LEND_USER_MEM, &gumr);
    } else {
        ret = gunyah_vm_ioctl(GH_VM_SET_USER_MEM_REGION, &gumr);
    }

    if (ret) {
        error_report("failed to add mem (%s)", strerror(errno));
        exit(1);
    }
}

static bool is_confidential_guest(void)
{
    return current_machine->cgs != NULL;
}

/*
 * Check if memory of a confidential VM needs to be split into two portions -
 * one private to it and other shared with host.
 */
static bool split_mem(GUNYAHState *s,
        MemoryRegion *area, MemoryRegionSection *section)
{
    bool writeable = !area->readonly && !area->rom_device;

    if (!is_confidential_guest()) {
        return false;
    }

    if (!s->swiotlb_size || section->size <= s->swiotlb_size) {
        return false;
    }

    /* Split only memory that can be written to by guest */
    if (!memory_region_is_ram(area) || !writeable) {
        return false;
    }

    /* Have we reserved already? */
    if (qatomic_read(&s->preshmem_reserved)) {
        return false;
    }

    /* Do we have enough available memory? */
    if (section->size <= s->swiotlb_size) {
        return false;
    }

    return true;
}

static void gunyah_set_phys_mem(GUNYAHState *s,
        MemoryRegionSection *section, bool add)
{
    MemoryRegion *area = section->mr;
    bool writable = !area->readonly && !area->rom_device;
    enum gh_mem_flags flags = 0;
    uint64_t page_size = qemu_real_host_page_size();
    MemoryRegionSection mrs = *section;
    bool lend = is_confidential_guest(), split = false;
    struct gunyah_slot *slot;

    /*
     * Gunyah hypervisor, at this time, does not support mapping memory
     * at low address (< 1GiB). Below code will be updated once
     * that limitation is addressed.
     */
    if (section->offset_within_address_space < GiB) {
        return;
    }

    if (!memory_region_is_ram(area)) {
        if (writable) {
            return;
        } else if (!memory_region_is_romd(area)) {
            /*
             * If the memory device is not in romd_mode, then we actually want
             * to remove the gunyah memory slot so all accesses will trap.
             */
             add = false;
        }
    }

    if (!QEMU_IS_ALIGNED(int128_get64(section->size), page_size) ||
        !QEMU_IS_ALIGNED(section->offset_within_address_space, page_size)) {
        error_report("Not page aligned");
        add = false;
    }

    gunyah_slots_lock(s);

    slot = gunyah_find_overlap_slot(s,
            section->offset_within_address_space,
            int128_get64(section->size));

    if (!add) {
        if (slot) {
            error_report("Memory slot removal not yet supported!");
            exit(1);
        }
        /* Nothing to be done as address range was not previously registered */
        goto done;
    } else {
        if (slot) {
            error_report("Overlapping slot registration not supported!");
            exit(1);
        }

        if (qatomic_read(&s->vm_started)) {
            error_report("Memory map changes after VM start not supported!");
            exit(1);
        }
    }

    if (area->readonly ||
        (!memory_region_is_ram(area) && memory_region_is_romd(area))) {
        flags = GH_MEM_ALLOW_READ | GH_MEM_ALLOW_EXEC;
    } else {
        flags = GH_MEM_ALLOW_READ | GH_MEM_ALLOW_WRITE | GH_MEM_ALLOW_EXEC;
    }

    split = split_mem(s, area, &mrs);
    if (split) {
        mrs.size -= s->swiotlb_size;
        gunyah_add_mem(s, &mrs, true, flags);
        lend = false;
        mrs.offset_within_region += mrs.size;
        mrs.offset_within_address_space += mrs.size;
        mrs.size = s->swiotlb_size;
        qatomic_set(&s->preshmem_reserved, true);
    }

    gunyah_add_mem(s, &mrs, lend, flags);

done:
    gunyah_slots_unlock(s);
}

static void gunyah_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    gunyah_set_phys_mem(s, section, true);
}

static void gunyah_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    gunyah_set_phys_mem(s, section, false);
}

void gunyah_set_swiotlb_size(uint64_t size)
{
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    s->swiotlb_size = size;
}

int gunyah_add_irqfd(int irqfd, int label, Error **errp)
{
    int ret;
    struct gh_fn_desc fdesc;
    struct gh_fn_irqfd_arg ghirqfd;

    fdesc.type = GH_FN_IRQFD;
    fdesc.arg_size = sizeof(struct gh_fn_irqfd_arg);
    fdesc.arg = (__u64)(&ghirqfd);

    ghirqfd.fd = irqfd;
    ghirqfd.label = label;
    ghirqfd.flags = GH_IRQFD_FLAGS_LEVEL;

    ret = gunyah_vm_ioctl(GH_VM_ADD_FUNCTION, &fdesc);
    if (ret) {
        error_setg_errno(errp, errno, "GH_FN_IRQFD failed");
    }

    return ret;
}

static int gunyah_set_ioeventfd_mmio(int fd, hwaddr addr,
        uint32_t size, uint32_t data, bool datamatch, bool assign)
{
    int ret;
    struct gh_fn_ioeventfd_arg io;
    struct gh_fn_desc fdesc;

    io.fd = fd;
    io.datamatch = datamatch ? data : 0;
    io.len = size;
    io.addr = addr;
    io.flags = datamatch ? GH_IOEVENTFD_FLAGS_DATAMATCH : 0;

    fdesc.type = GH_FN_IOEVENTFD;
    fdesc.arg_size = sizeof(struct gh_fn_ioeventfd_arg);
    fdesc.arg = (__u64)(&io);

    if (assign) {
        ret = gunyah_vm_ioctl(GH_VM_ADD_FUNCTION, &fdesc);
    } else {
        ret = gunyah_vm_ioctl(GH_VM_REMOVE_FUNCTION, &fdesc);
    }

    return ret;
}

static void gunyah_mem_ioeventfd_add(MemoryListener *listener,
                                  MemoryRegionSection *section,
                                  bool match_data, uint64_t data,
                                  EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int r;

    r = gunyah_set_ioeventfd_mmio(fd, section->offset_within_address_space,
                               int128_get64(section->size), data, match_data,
                               true);
    if (r < 0) {
        error_report("error adding ioeventfd: %s", strerror(errno));
        exit(1);
    }
}

static void gunyah_mem_ioeventfd_del(MemoryListener *listener,
                                  MemoryRegionSection *section,
                                  bool match_data, uint64_t data,
                                  EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int r;

    r = gunyah_set_ioeventfd_mmio(fd, section->offset_within_address_space,
                               int128_get64(section->size), data, match_data,
                               false);
    if (r < 0) {
        error_report("error deleting ioeventfd: %s", strerror(errno));
        exit(1);
    }
}

GUNYAHState *get_gunyah_state(void)
{
    return GUNYAH_STATE(current_accel());
}

static void gunyah_ipi_signal(int sig)
{
    if (current_cpu) {
        qatomic_set(&current_cpu->accel->run->immediate_exit, 1);
    }
}

static void gunyah_cpu_kick_self(void)
{
    qatomic_set(&current_cpu->accel->run->immediate_exit, 1);
}

static int gunyah_init_vcpu(CPUState *cpu, Error **errp)
{
    int ret;
    struct gh_fn_desc fdesc;
    struct gh_fn_vcpu_arg vcpu;
    struct sigaction sigact;
    sigset_t set;

    cpu->accel = g_new0(AccelCPUState, 1);

    /* init cpu signals */
    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = gunyah_ipi_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    pthread_sigmask(SIG_BLOCK, NULL, &set);
    sigdelset(&set, SIG_IPI);

    ret = pthread_sigmask(SIG_SETMASK, &set, NULL);
    if (ret) {
        error_report("pthread_sigmask: %s", strerror(ret));
        exit(1);
    }

    vcpu.id = cpu->cpu_index;
    fdesc.type = GH_FN_VCPU;
    fdesc.arg_size = sizeof(struct gh_fn_vcpu_arg);
    fdesc.arg = (__u64)(&vcpu);

    ret = gunyah_vm_ioctl(GH_VM_ADD_FUNCTION, &fdesc);
    if (ret < 0) {
        error_report("could not create VCPU %d: %s", vcpu.id, strerror(errno));
        exit(1);
    }

    cpu->accel->fd = ret;
    cpu->accel->run = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, ret, 0);
    if (cpu->accel->run == MAP_FAILED) {
        error_report("mmap of vcpu run structure failed : %s", strerror(errno));
        exit(1);
    }

    return 0;
}

static void gunyah_vcpu_destroy(CPUState *cpu)
{
    int ret;

    ret = munmap(cpu->accel->run, 4096);
    if (ret < 0) {
        error_report("munmap of vcpu run structure failed: %s",
                strerror(errno));
        exit(1);
    }

    close(cpu->accel->fd);
    g_free(cpu->accel);
}

void gunyah_start_vm(void)
{
    int ret;
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    ret = gunyah_vm_ioctl(GH_VM_START);
    if (ret != 0) {
        error_report("Failed to start VM: %s", strerror(errno));
        exit(1);
    }
    qatomic_set(&s->vm_started, 1);
}

static int gunyah_vcpu_exec(CPUState *cpu)
{
    int ret;
    enum gh_vm_status exit_status;
    enum gh_vm_exit_type exit_type;

    bql_unlock();
    cpu_exec_start(cpu);

    do {
        struct gh_vcpu_run *run = cpu->accel->run;
        int exit_reason;

        if (qatomic_read(&cpu->exit_request)) {
            gunyah_cpu_kick_self();
        }

        /* Todo: Check need for smp_rmb() here */

        ret = gunyah_vcpu_ioctl(cpu, GH_VCPU_RUN);
        if (ret < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                qatomic_set(&run->immediate_exit, 0);
                /* Todo: Check need for smp_wmb() here */
                ret = EXCP_INTERRUPT;
                break;
            }

            error_report("GH_VCPU_RUN: %s", strerror(errno));
            ret = -1;
            break;
        }

        exit_reason = run->exit_reason;
        switch (exit_reason) {
        case GH_VCPU_EXIT_MMIO:
            address_space_rw(&address_space_memory,
                run->mmio.phys_addr, MEMTXATTRS_UNSPECIFIED,
                run->mmio.data,
                run->mmio.len,
                run->mmio.is_write);
            break;

        case GH_VCPU_EXIT_STATUS:
            exit_status = run->status.status;
            exit_type = run->status.exit_info.type;

            switch (exit_status) {
            case GH_VM_STATUS_CRASHED:
                    bql_lock();
                    qemu_system_guest_panicked(NULL);
                    bql_unlock();
                    ret = EXCP_INTERRUPT;
                    break;
            case GH_VM_STATUS_EXITED:
                /* Fall-through */
            default:
                switch (exit_type) {
                case GH_RM_EXIT_TYPE_WDT_BITE:
                    bql_lock();
                    qemu_system_guest_panicked(NULL);
                    bql_unlock();
                    ret = EXCP_INTERRUPT;
                    break;

                case GH_RM_EXIT_TYPE_PSCI_SYSTEM_RESET:
                case GH_RM_EXIT_TYPE_PSCI_SYSTEM_RESET2:
                    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                    ret = EXCP_INTERRUPT;
                    break;
                case GH_RM_EXIT_TYPE_VM_EXIT:
                case GH_RM_EXIT_TYPE_PSCI_POWER_OFF:
                    /* Fall-through */
                default:
                    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
                    ret = EXCP_INTERRUPT;
                }
            }
            break;

        default:
            error_report("unhandled exit %d", exit_reason);
            exit(1);
        }
    } while (ret == 0);

    cpu_exec_end(cpu);
    bql_lock();

    if (ret < 0) {
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    qatomic_set(&cpu->exit_request, 0);

    return ret;
}

void *gunyah_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    rcu_register_thread();

    bql_lock();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    cpu->neg.can_do_io = true;
    current_cpu = cpu;

    gunyah_init_vcpu(cpu, &error_fatal);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            gunyah_vcpu_exec(cpu);
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    gunyah_vcpu_destroy(cpu);
    cpu_thread_signal_destroyed(cpu);
    bql_unlock();
    rcu_unregister_thread();
    return NULL;
}

static void do_gunyah_cpu_synchronize_post_reset(CPUState *cpu,
                                run_on_cpu_data arg)
{
    gunyah_arch_put_registers(cpu, 0);
    cpu->vcpu_dirty = false;
}

void gunyah_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_gunyah_cpu_synchronize_post_reset, RUN_ON_CPU_NULL);
}
