/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors:
 *  Magnus Kulke      <magnuskulke@microsoft.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "linux/mshv.h"
#include "system/address-spaces.h"
#include "system/mshv.h"
#include "exec/memattrs.h"
#include <sys/ioctl.h>
#include "trace.h"

MshvMemorySlot mem_slots[MSHV_MAX_MEM_SLOTS];

static MshvMemorySlot *find_free_mem_slot(void)
{
    for (int i = 0; i < MSHV_MAX_MEM_SLOTS; i++) {
        if (mem_slots[i].memory_size == 0) {
            return &mem_slots[i];
        }
    }

    return NULL;
}

/* Find _currently mapped_ memory slot, that is overlapping in userspace */
static MshvMemorySlot *find_overlap_mem_slot(const MshvMemorySlot *slot)
{
    MshvMemorySlot *other;
    bool overlaps;
    uint64_t start_1 = slot->userspace_addr, start_2;
    size_t len_1 = slot->memory_size, len_2;

    for (int i = 0; i < MSHV_MAX_MEM_SLOTS; i++) {
        other = &mem_slots[i];

        if (other == slot) {
            continue;
        }

        start_2 = other->userspace_addr;
        len_2 = other->memory_size;

        overlaps = ranges_overlap(start_1, len_1, start_2, len_2);
        if (other->mapped && overlaps) {
            return other;
        }
    }

    return NULL;
}

static int set_guest_memory(int vm_fd, const mshv_user_mem_region *region)
{
    int ret;

    ret = ioctl(vm_fd, MSHV_SET_GUEST_MEMORY, region);
    if (ret < 0) {
        error_report("failed to set guest memory: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int map_or_unmap(int vm_fd, const MshvMemorySlot *slot, bool map)
{
    struct mshv_user_mem_region region = {0};

    region.guest_pfn = slot->guest_phys_addr >> MSHV_PAGE_SHIFT;
    region.size = slot->memory_size;
    region.userspace_addr = slot->userspace_addr;

    if (!map) {
        region.flags |= (1 << MSHV_SET_MEM_BIT_UNMAP);
        trace_mshv_unmap_memory(slot->userspace_addr, slot->guest_phys_addr,
                                slot->memory_size);
        return set_guest_memory(vm_fd, &region);
    }

    region.flags = BIT(MSHV_SET_MEM_BIT_EXECUTABLE);
    if (!slot->readonly) {
        region.flags |= BIT(MSHV_SET_MEM_BIT_WRITABLE);
    }

    trace_mshv_map_memory(slot->userspace_addr, slot->guest_phys_addr,
                          slot->memory_size);
    return set_guest_memory(vm_fd, &region);
}

static MshvMemorySlot *find_mem_slot_by_region(uint64_t gpa, uint64_t size,
                                               uint64_t userspace_addr)
{
    MshvMemorySlot *slot;

    for (int i = 0; i < MSHV_MAX_MEM_SLOTS; i++) {
        slot = &mem_slots[i];

        if (slot->guest_phys_addr == gpa &&
            slot->userspace_addr  == userspace_addr &&
            slot->memory_size     == size) {
            trace_mshv_found_slot(slot->userspace_addr, slot->guest_phys_addr,
                                  slot->memory_size);
            return slot;
        }
    }

    return NULL;
}

static MshvMemorySlot* find_mem_slot_by_gpa(uint64_t gpa)
{
    uint64_t gpa_offset;
    MshvMemorySlot *slot;

    trace_mshv_find_slot_by_gpa(gpa);

    for (int i = 0; i < MSHV_MAX_MEM_SLOTS; i++) {
        slot = &mem_slots[i];

        gpa_offset = gpa - slot->guest_phys_addr;
        if (slot->guest_phys_addr <= gpa && gpa_offset < slot->memory_size) {
            trace_mshv_found_slot(slot->userspace_addr, slot->guest_phys_addr,
                                  slot->memory_size);
            return slot;
        }
    }

    return NULL;
}

MshvRemapResult mshv_remap_overlap_region(int vm_fd, uint64_t gpa)
{
    MshvMemorySlot *gpa_slot, *overlap_slot;
    int ret;

    /* return early if no slot is found */
    gpa_slot = find_mem_slot_by_gpa(gpa);
    if (gpa_slot == NULL) {
        return MshvRemapNoMapping;
    }

    overlap_slot = find_overlap_mem_slot(gpa_slot);
    if (overlap_slot == NULL) {
        return MshvRemapNoOverlap;
    }

    /* unmap overlapping slot */
    ret = map_or_unmap(vm_fd, overlap_slot, false);
    if (ret < 0) {
        error_report("failed to unmap overlap region");
        abort();
    }
    overlap_slot->mapped = false;
    warn_report("mapped out userspace_addr=0x%016lx gpa=0x%010lx size=0x%lx",
                overlap_slot->userspace_addr,
                overlap_slot->guest_phys_addr,
                overlap_slot->memory_size);

    /* map region for gpa */
    ret = map_or_unmap(vm_fd, gpa_slot, true);
    if (ret < 0) {
        error_report("failed to map new region");
        abort();
    }
    gpa_slot->mapped = true;
    warn_report("mapped in  userspace_addr=0x%016lx gpa=0x%010lx size=0x%lx",
                gpa_slot->userspace_addr, gpa_slot->guest_phys_addr,
                gpa_slot->memory_size);

    return MshvRemapOk;
}

static int handle_unmapped_mmio_region_read(uint64_t gpa, uint64_t size,
                                            uint8_t *data)
{
    warn_report("read from unmapped mmio region gpa=0x%lx size=%lu", gpa, size);

    if (size == 0 || size > 8) {
        error_report("invalid size %lu for reading from unmapped mmio region",
                     size);
        return -1;
    }

    memset(data, 0xFF, size);

    return 0;
}

int mshv_guest_mem_read(uint64_t gpa, uint8_t *data, uintptr_t size,
                        bool is_secure_mode, bool instruction_fetch)
{
    int ret;
    MemTxAttrs memattr = { .secure = is_secure_mode };

    if (instruction_fetch) {
        trace_mshv_insn_fetch(gpa, size);
    } else {
        trace_mshv_mem_read(gpa, size);
    }

    ret = address_space_rw(&address_space_memory, gpa, memattr, (void *)data,
                           size, false);
    if (ret == MEMTX_OK) {
        return 0;
    }

    if (ret == MEMTX_DECODE_ERROR) {
        return handle_unmapped_mmio_region_read(gpa, size, data);
    }

    error_report("failed to read guest memory at 0x%lx", gpa);
    return -1;
}

int mshv_guest_mem_write(uint64_t gpa, const uint8_t *data, uintptr_t size,
                         bool is_secure_mode)
{
    int ret;
    MemTxAttrs memattr = { .secure = is_secure_mode };

    trace_mshv_mem_write(gpa, size);
    ret = address_space_rw(&address_space_memory, gpa, memattr, (void *)data,
                           size, true);
    if (ret == MEMTX_OK) {
        return 0;
    }

    if (ret == MEMTX_DECODE_ERROR) {
        warn_report("write to unmapped mmio region gpa=0x%lx size=%lu", gpa,
                    size);
        return 0;
    }

    error_report("Failed to write guest memory");
    return -1;
}

static void clear_slot(MshvMemorySlot *slot)
{
    assert(slot);

    *slot = (MshvMemorySlot) { 0 };
}

static int tracked_unmap(int vm_fd, uint64_t gpa, uint64_t size,
                        uint64_t userspace_addr)
{
    int ret;
    MshvMemorySlot *slot;

    slot = find_mem_slot_by_region(gpa, size, userspace_addr);
    if (!slot) {
        trace_mshv_skip_unset_mem(userspace_addr, gpa, size);
        /* no work to do */
        return 0;
    }

    if (!slot->mapped) {
        /* remove slot, no need to unmap */
        clear_slot(slot);
        return 0;
    }

    ret = map_or_unmap(vm_fd, slot, false);
    if (ret < 0) {
        error_report("failed to unmap memory region");
        return ret;
    }
    clear_slot(slot);

    return 0;
}

static int tracked_map(int vm_fd, uint64_t gpa, uint64_t size, bool readonly,
                       uint64_t userspace_addr)
{
    MshvMemorySlot *slot, *overlap_slot;
    int ret;

    slot = find_mem_slot_by_region(gpa, size, userspace_addr);
    if (slot) {
        error_report("memory region already mapped at gpa=0x%lx, "
                     "userspace_addr=0x%lx, size=0x%lx",
                     slot->guest_phys_addr, slot->userspace_addr,
                     slot->memory_size);
        return -1;
    }

    slot = find_free_mem_slot();
    if (!slot) {
        error_report("no free memory slot available");
        return -1;
    }

    slot->guest_phys_addr = gpa;
    slot->userspace_addr = userspace_addr;
    slot->memory_size = size;
    slot->readonly = readonly;

    overlap_slot = find_overlap_mem_slot(slot);
    if (overlap_slot) {
        trace_mshv_remap_attempt(slot->userspace_addr,
                                 slot->guest_phys_addr,
                                 slot->memory_size);
        warn_report("attempt to map region [0x%lx-0x%lx], while "
                    "[0x%lx-0x%lx] is already mapped in the guest",
                    userspace_addr, userspace_addr + size - 1,
                    overlap_slot->userspace_addr,
                    overlap_slot->userspace_addr +
                    overlap_slot->memory_size - 1);

        /* do not register mem slot in hv, but record for later swap-in */
        slot->mapped = false;

        return 0;
    }

    ret = map_or_unmap(vm_fd, slot, true);
    if (ret < 0) {
        error_report("failed to map memory region");
        return -1;
    }
    slot->mapped = true;

    return 0;
}

static int set_memory(uint64_t gpa, uint64_t size, bool readonly,
                      uint64_t userspace_addr, bool add)
{
    int vm_fd = mshv_state->vm;

    if (add) {
        return tracked_map(vm_fd, gpa, size, readonly, userspace_addr);
    }

    return tracked_unmap(vm_fd, gpa, size, userspace_addr);
}

/*
 * Calculate and align the start address and the size of the section.
 * Return the size. If the size is 0, the aligned section is empty.
 */
static hwaddr align_section(MemoryRegionSection *section, hwaddr *start)
{
    hwaddr size = int128_get64(section->size);
    hwaddr delta, aligned;

    /*
     * works in page size chunks, but the function may be called
     * with sub-page size and unaligned start address. Pad the start
     * address to next and truncate size to previous page boundary.
     */
    aligned = ROUND_UP(section->offset_within_address_space,
                       qemu_real_host_page_size());
    delta = aligned - section->offset_within_address_space;
    *start = aligned;
    if (delta > size) {
        return 0;
    }

    return (size - delta) & qemu_real_host_page_mask();
}

void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add)
{
    int ret = 0;
    MemoryRegion *area = section->mr;
    bool writable = !area->readonly && !area->rom_device;
    hwaddr start_addr, mr_offset, size;
    void *ram;

    size = align_section(section, &start_addr);

    trace_mshv_set_phys_mem(add, section->mr->name, start_addr);

    /* If the memory device is a writable non-ram area, we do not
     * want to map it into the guest memory. If it is not a ROM device,
     * we want to remove mshv memory mapping, so accesses will trap.
     */
    if (!memory_region_is_ram(area)) {
        if (writable) {
            return;
        } else if (!area->romd_mode) {
            add = false;
        }
    }

    if (!size) {
        return;
    }

    mr_offset = section->offset_within_region + start_addr -
                section->offset_within_address_space;

    ram = memory_region_get_ram_ptr(area) + mr_offset;

    ret = set_memory(start_addr, size, !writable, (uint64_t)ram, add);
    if (ret < 0) {
        error_report("failed to set memory region");
        abort();
    }
}
