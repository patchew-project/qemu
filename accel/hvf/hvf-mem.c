/*
 * Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 * Copyright 2011 Intel Corporation
 * Copyright 2016 Veertu, Inc.
 * Copyright 2017 The Android Open Source Project
 *
 * QEMU Hypervisor.framework support
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "sysemu/hvf.h"
#include "sysemu/hvf_int.h"

/* Memory slots */

#define HVF_NUM_SLOTS 32

/* HVFSlot flags */
#define HVF_SLOT_LOG (1 << 0)
#define HVF_SLOT_READONLY (1 << 1)

typedef struct HVFSlot {
    hwaddr start;
    hwaddr size;  /* 0 if the slot is free */
    hwaddr offset;  /* offset within memory region */
    uint32_t flags;
    MemoryRegion *region;
} HVFSlot;

static HVFSlot memslots[HVF_NUM_SLOTS];
static QemuMutex memlock;

static HVFSlot *hvf_find_overlap_slot(hwaddr start, hwaddr size)
{
    HVFSlot *slot;
    int x;
    for (x = 0; x < HVF_NUM_SLOTS; ++x) {
        slot = &memslots[x];
        if (slot->size && start < (slot->start + slot->size) &&
            (start + size) > slot->start) {
            return slot;
        }
    }
    return NULL;
}

static HVFSlot *hvf_find_free_slot(void)
{
    HVFSlot *slot;
    int x;
    for (x = 0; x < HVF_NUM_SLOTS; x++) {
        slot = &memslots[x];
        if (!slot->size) {
            return slot;
        }
    }

    return NULL;
}

/*
 * Hypervisor.framework requires that the host virtual address,
 * the guest physical address and the size of memory regions are aligned
 * to the host page size.
 *
 * The function here tries to align the guest physical address and the size.
 *
 * The return value is the aligned size.
 * The aligned guest physical address will be written to `start'.
 * The delta between the aligned address and the original address will be
 * written to `delta'.
 */
static hwaddr hvf_align_section(MemoryRegionSection *section,
                              hwaddr *start, hwaddr *delta)
{
    hwaddr unaligned, _start, size, _delta;

    unaligned = section->offset_within_address_space;
    size = int128_get64(section->size);
    _start = ROUND_UP(unaligned, qemu_real_host_page_size);
    _delta = _start - unaligned;
    size = (size - _delta) & qemu_real_host_page_mask;

    *start = _start;
    *delta = _delta;

    return size;
}

static void hvf_set_phys_mem(MemoryRegionSection *section, bool add)
{
    HVFSlot *slot;
    hwaddr start, size, offset, delta;
    uint8_t *host_addr;
    MemoryRegion *area = section->mr;
    bool readonly, dirty_tracking;
    hv_memory_flags_t flags;
    hv_return_t ret;

    if (add && !memory_region_is_ram(area) && !memory_region_is_romd(area)) {
        /*
         * If the memory region is not RAM and is in ROMD mode,
         * do not map it to the guest.
         */
        return;
    }

    size = hvf_align_section(section, &start, &delta);

    if (!size) {
        /* The size is 0 after aligned. Do not map the region */
        return;
    }

    if (add) {
        /* add memory region */
        offset = section->offset_within_region + delta;
        host_addr = memory_region_get_ram_ptr(area) + offset;

        if (!QEMU_PTR_IS_ALIGNED(host_addr, qemu_real_host_page_size)) {
            /* The host virtual address is not aligned. It cannot be mapped */
            return;
        }

        dirty_tracking = !!memory_region_get_dirty_log_mask(area);
        readonly = memory_region_is_rom(area) || memory_region_is_romd(area);

        /* setup a slot */
        qemu_mutex_lock(&memlock);

        slot = hvf_find_free_slot();
        if (!slot) {
            error_report("No free slots");
            abort();
        }

        slot->start = start;
        slot->size = size;
        slot->offset = offset;
        slot->flags = 0;
        slot->region = area;

        if (readonly) {
            slot->flags |= HVF_SLOT_READONLY;
        }

        if (dirty_tracking) {
            slot->flags |= HVF_SLOT_LOG;
        }

        /* set Hypervisor.framework memory mapping flags */
        if (readonly || dirty_tracking) {
            flags = HV_MEMORY_READ | HV_MEMORY_EXEC;
        } else {
            flags = HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC;
        }

        ret = hv_vm_map(host_addr, start, size, flags);
        assert_hvf_ok(ret);

        qemu_mutex_unlock(&memlock);
    } else {
        /* remove memory region */
        qemu_mutex_lock(&memlock);

        slot = hvf_find_overlap_slot(start, size);

        if (slot) {
            ret = hv_vm_unmap(start, size);
            assert_hvf_ok(ret);

            slot->size = 0;
        }

        qemu_mutex_unlock(&memlock);
    }
}

static void hvf_set_dirty_tracking(MemoryRegionSection *section, bool on)
{
    HVFSlot *slot;

    qemu_mutex_lock(&memlock);

    slot = hvf_find_overlap_slot(
            section->offset_within_address_space,
            int128_get64(section->size));

    /* protect region against writes; begin tracking it */
    if (on) {
        slot->flags |= HVF_SLOT_LOG;
        hv_vm_protect((uintptr_t)slot->start, (size_t)slot->size,
                      HV_MEMORY_READ | HV_MEMORY_EXEC);
    /* stop tracking region*/
    } else {
        slot->flags &= ~HVF_SLOT_LOG;
        hv_vm_protect((uintptr_t)slot->start, (size_t)slot->size,
                      HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    }

    qemu_mutex_unlock(&memlock);
}

static void hvf_log_start(MemoryListener *listener,
                          MemoryRegionSection *section, int old, int new)
{
    if (old == new) {
        return;
    }

    hvf_set_dirty_tracking(section, 1);
}

static void hvf_log_stop(MemoryListener *listener,
                         MemoryRegionSection *section, int old, int new)
{
    if (new != 0) {
        return;
    }

    hvf_set_dirty_tracking(section, 0);
}

static void hvf_log_clear(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    /*
     * The dirty bits are being cleared.
     * Make the section write-protected again.
     */
    hvf_set_dirty_tracking(section, 1);
}

static void hvf_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    hvf_set_phys_mem(section, true);
}

static void hvf_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    hvf_set_phys_mem(section, false);
}

static MemoryListener hvf_memory_listener = {
    .name = "hvf",
    .priority = 10,
    .region_add = hvf_region_add,
    .region_del = hvf_region_del,
    .log_start = hvf_log_start,
    .log_stop = hvf_log_stop,
    .log_clear = hvf_log_clear,
};


/*
 * The function is called when the guest is accessing memory causing vmexit.
 * Check whether the guest can access the memory directly and
 * also mark the accessed page being written dirty
 * if the page is being dirty-tracked.
 *
 * Return true if the access is within the mapped region,
 * otherwise return false.
 */
bool hvf_access_memory(hwaddr address, bool write)
{
    HVFSlot *slot;
    hv_return_t ret;
    hwaddr start, size;

    qemu_mutex_lock(&memlock);

    slot = hvf_find_overlap_slot(address, 1);

    if (!slot || (write && slot->flags & HVF_SLOT_READONLY)) {
        /* MMIO or unmapped area, return false */
        qemu_mutex_unlock(&memlock);
        return false;
    }

    if (write && (slot->flags & HVF_SLOT_LOG)) {
        /* The slot is being dirty-tracked. Mark the accessed page dirty. */
        start = address & qemu_real_host_page_mask;
        size = qemu_real_host_page_size;

        memory_region_set_dirty(slot->region,
                                start - slot->start + slot->offset, size);
        ret = hv_vm_protect(start, size,
                    HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
        assert_hvf_ok(ret);
    }

    qemu_mutex_unlock(&memlock);
    return true;
}

void hvf_init_memslots(void)
{
    qemu_mutex_init(&memlock);
    memory_listener_register(&hvf_memory_listener, &address_space_memory);
}
