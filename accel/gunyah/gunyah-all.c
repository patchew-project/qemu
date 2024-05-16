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

void *gunyah_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    do {
        /* Do nothing */
    } while (!cpu->unplug || cpu_can_run(cpu));

    return NULL;
}
