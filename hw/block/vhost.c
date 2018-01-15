/*
 * vhost support
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/vhost.h"
#include "hw/hw.h"
#include "qemu/atomic.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "qemu/memfd.h"
#include <linux/vhost.h>
#include "exec/address-spaces.h"
#include "hw/virtio/virtio-bus.h"
#include "migration/blocker.h"
#include "sysemu/dma.h"

#include "vhost_user_nvme.h"

static unsigned int used_memslots;
static QLIST_HEAD(, vhost_dev) vhost_devices =
    QLIST_HEAD_INITIALIZER(vhost_devices);

/* Assign/unassign. Keep an unsorted array of non-overlapping
 * memory regions in dev->mem. */
static void vhost_dev_unassign_memory(struct vhost_dev *dev,
                                      uint64_t start_addr,
                                      uint64_t size)
{
    int from, to, n = dev->mem->nregions;
    /* Track overlapping/split regions for sanity checking. */
    int overlap_start = 0, overlap_end = 0, overlap_middle = 0, split = 0;

    for (from = 0, to = 0; from < n; ++from, ++to) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        uint64_t reglast;
        uint64_t memlast;
        uint64_t change;

        /* clone old region */
        if (to != from) {
            memcpy(reg, dev->mem->regions + from, sizeof *reg);
        }

        /* No overlap is simple */
        if (!ranges_overlap(reg->guest_phys_addr, reg->memory_size,
                            start_addr, size)) {
            continue;
        }

        /* Split only happens if supplied region
         * is in the middle of an existing one. Thus it can not
         * overlap with any other existing region. */
        assert(!split);

        reglast = range_get_last(reg->guest_phys_addr, reg->memory_size);
        memlast = range_get_last(start_addr, size);

        /* Remove whole region */
        if (start_addr <= reg->guest_phys_addr && memlast >= reglast) {
            --dev->mem->nregions;
            --to;
            ++overlap_middle;
            continue;
        }

        /* Shrink region */
        if (memlast >= reglast) {
            reg->memory_size = start_addr - reg->guest_phys_addr;
            assert(reg->memory_size);
            assert(!overlap_end);
            ++overlap_end;
            continue;
        }

        /* Shift region */
        if (start_addr <= reg->guest_phys_addr) {
            change = memlast + 1 - reg->guest_phys_addr;
            reg->memory_size -= change;
            reg->guest_phys_addr += change;
            reg->userspace_addr += change;
            assert(reg->memory_size);
            assert(!overlap_start);
            ++overlap_start;
            continue;
        }

        /* This only happens if supplied region
         * is in the middle of an existing one. Thus it can not
         * overlap with any other existing region. */
        assert(!overlap_start);
        assert(!overlap_end);
        assert(!overlap_middle);
        /* Split region: shrink first part, shift second part. */
        memcpy(dev->mem->regions + n, reg, sizeof *reg);
        reg->memory_size = start_addr - reg->guest_phys_addr;
        assert(reg->memory_size);
        change = memlast + 1 - reg->guest_phys_addr;
        reg = dev->mem->regions + n;
        reg->memory_size -= change;
        assert(reg->memory_size);
        reg->guest_phys_addr += change;
        reg->userspace_addr += change;
        /* Never add more than 1 region */
        assert(dev->mem->nregions == n);
        ++dev->mem->nregions;
        ++split;
    }
}

/* Called after unassign, so no regions overlap the given range. */
static void vhost_dev_assign_memory(struct vhost_dev *dev,
                                    uint64_t start_addr,
                                    uint64_t size,
                                    uint64_t uaddr)
{
    int from, to;
    struct vhost_memory_region *merged = NULL;
    for (from = 0, to = 0; from < dev->mem->nregions; ++from, ++to) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        uint64_t prlast, urlast;
        uint64_t pmlast, umlast;
        uint64_t s, e, u;

        /* clone old region */
        if (to != from) {
            memcpy(reg, dev->mem->regions + from, sizeof *reg);
        }
        prlast = range_get_last(reg->guest_phys_addr, reg->memory_size);
        pmlast = range_get_last(start_addr, size);
        urlast = range_get_last(reg->userspace_addr, reg->memory_size);
        umlast = range_get_last(uaddr, size);

        /* check for overlapping regions: should never happen. */
        assert(prlast < start_addr || pmlast < reg->guest_phys_addr);
        /* Not an adjacent or overlapping region - do not merge. */
        if ((prlast + 1 != start_addr || urlast + 1 != uaddr) &&
            (pmlast + 1 != reg->guest_phys_addr ||
             umlast + 1 != reg->userspace_addr)) {
            continue;
        }

        if (dev->vhost_ops->vhost_backend_can_merge &&
            !dev->vhost_ops->vhost_backend_can_merge(dev, uaddr, size,
                                                     reg->userspace_addr,
                                                     reg->memory_size)) {
            continue;
        }

        if (merged) {
            --to;
            assert(to >= 0);
        } else {
            merged = reg;
        }
        u = MIN(uaddr, reg->userspace_addr);
        s = MIN(start_addr, reg->guest_phys_addr);
        e = MAX(pmlast, prlast);
        uaddr = merged->userspace_addr = u;
        start_addr = merged->guest_phys_addr = s;
        size = merged->memory_size = e - s + 1;
        assert(merged->memory_size);
    }

    if (!merged) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        memset(reg, 0, sizeof *reg);
        reg->memory_size = size;
        assert(reg->memory_size);
        reg->guest_phys_addr = start_addr;
        reg->userspace_addr = uaddr;
        ++to;
    }
    assert(to <= dev->mem->nregions + 1);
    dev->mem->nregions = to;
}

static struct vhost_memory_region *vhost_dev_find_reg(struct vhost_dev *dev,
                                                      uint64_t start_addr,
                                                      uint64_t size)
{
    int i, n = dev->mem->nregions;
    for (i = 0; i < n; ++i) {
        struct vhost_memory_region *reg = dev->mem->regions + i;
        if (ranges_overlap(reg->guest_phys_addr, reg->memory_size,
                           start_addr, size)) {
            return reg;
        }
    }
    return NULL;
}

static bool vhost_dev_cmp_memory(struct vhost_dev *dev,
                                 uint64_t start_addr,
                                 uint64_t size,
                                 uint64_t uaddr)
{
    struct vhost_memory_region *reg = vhost_dev_find_reg(dev, start_addr, size);
    uint64_t reglast;
    uint64_t memlast;

    if (!reg) {
        return true;
    }

    reglast = range_get_last(reg->guest_phys_addr, reg->memory_size);
    memlast = range_get_last(start_addr, size);

    /* Need to extend region? */
    if (start_addr < reg->guest_phys_addr || memlast > reglast) {
        return true;
    }
    /* userspace_addr changed? */
    return uaddr != reg->userspace_addr + start_addr - reg->guest_phys_addr;
}

static void vhost_set_memory(MemoryListener *listener,
                             MemoryRegionSection *section,
                             bool add)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    bool log_dirty =
        memory_region_get_dirty_log_mask(section->mr) &
                                         ~(1 << DIRTY_MEMORY_MIGRATION);
    int s = offsetof(struct vhost_memory, regions) +
        (dev->mem->nregions + 1) * sizeof dev->mem->regions[0];
    void *ram;

    dev->mem = g_realloc(dev->mem, s);

    if (log_dirty) {
        add = false;
    }

    assert(size);

    /* Optimize no-change case. At least cirrus_vga does
     * this a lot at this time.
     */
    ram = memory_region_get_ram_ptr(section->mr) +
                                    section->offset_within_region;
    if (add) {
        if (!vhost_dev_cmp_memory(dev, start_addr, size, (uintptr_t)ram)) {
            /* Region exists with same address. Nothing to do. */
            return;
        }
    } else {
        if (!vhost_dev_find_reg(dev, start_addr, size)) {
            /* Removing region that we don't access. Nothing to do. */
            return;
        }
    }

    vhost_dev_unassign_memory(dev, start_addr, size);
    if (add) {
        /* Add given mapping, merging adjacent regions if any */
        vhost_dev_assign_memory(dev, start_addr, size, (uintptr_t)ram);
    } else {
        /* Remove old mapping for this memory, if any. */
        vhost_dev_unassign_memory(dev, start_addr, size);
    }
    dev->mem_changed_start_addr = MIN(dev->mem_changed_start_addr, start_addr);
    dev->mem_changed_end_addr = MAX(dev->mem_changed_end_addr,
                                    start_addr + size - 1);
    dev->memory_changed = true;
    used_memslots = dev->mem->nregions;
}

static bool vhost_section(MemoryRegionSection *section)
{
    return memory_region_is_ram(section->mr) &&
        !memory_region_is_rom(section->mr);
}

static void vhost_begin(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    dev->mem_changed_end_addr = 0;
    dev->mem_changed_start_addr = -1;
}

static void vhost_commit(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    int r;

    if (!dev->memory_changed) {
        return;
    }
    if (!dev->started) {
        return;
    }
    if (dev->mem_changed_start_addr > dev->mem_changed_end_addr) {
        return;
    }

    r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
    if (r < 0) {
        error_report("vhost_set_mem_table failed");
    }
    dev->memory_changed = false;
}

static void vhost_region_add(MemoryListener *listener,
                             MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);

    if (!vhost_section(section)) {
        return;
    }

    ++dev->n_mem_sections;
    dev->mem_sections = g_renew(MemoryRegionSection, dev->mem_sections,
                                dev->n_mem_sections);
    dev->mem_sections[dev->n_mem_sections - 1] = *section;
    memory_region_ref(section->mr);
    vhost_set_memory(listener, section, true);
}

static void vhost_region_del(MemoryListener *listener,
                             MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    int i;

    if (!vhost_section(section)) {
        return;
    }

    vhost_set_memory(listener, section, false);
    memory_region_unref(section->mr);
    for (i = 0; i < dev->n_mem_sections; ++i) {
        if (dev->mem_sections[i].offset_within_address_space
            == section->offset_within_address_space) {
            --dev->n_mem_sections;
            memmove(&dev->mem_sections[i], &dev->mem_sections[i + 1],
                    (dev->n_mem_sections - i) * sizeof(*dev->mem_sections));
            break;
        }
    }
}

static void vhost_region_nop(MemoryListener *listener,
                             MemoryRegionSection *section)
{
}

static void vhost_eventfd_add(MemoryListener *listener,
                              MemoryRegionSection *section,
                              bool match_data, uint64_t data, EventNotifier *e)
{
}

static void vhost_eventfd_del(MemoryListener *listener,
                              MemoryRegionSection *section,
                              bool match_data, uint64_t data, EventNotifier *e)
{
}

int vhost_dev_nvme_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
   int r;

   r = vhost_dev_nvme_set_backend_type(hdev, backend_type);
   assert(r >= 0);

   r = hdev->vhost_ops->vhost_backend_init(hdev, opaque);
   if (r < 0) {
        return -1;
   }

   hdev->memory_listener = (MemoryListener) {
        .begin = vhost_begin,
        .commit = vhost_commit,
        .region_add = vhost_region_add,
        .region_del = vhost_region_del,
        .region_nop = vhost_region_nop,
        .eventfd_add = vhost_eventfd_add,
        .eventfd_del = vhost_eventfd_del,
        .priority = 10
    };

    hdev->mem = g_malloc0(offsetof(struct vhost_memory, regions));
    hdev->n_mem_sections = 0;
    hdev->mem_sections = NULL;
    hdev->log = NULL;
    hdev->log_size = 0;
    hdev->log_enabled = false;
    hdev->started = false;
    hdev->memory_changed = false;
    memory_listener_register(&hdev->memory_listener, &address_space_memory);
    QLIST_INSERT_HEAD(&vhost_devices, hdev, entry);
    return 0;
}

void vhost_dev_nvme_cleanup(struct vhost_dev *hdev)
{
    if (hdev->mem) {
        /* those are only safe after successful init */
        memory_listener_unregister(&hdev->memory_listener);
        QLIST_REMOVE(hdev, entry);
    }
    g_free(hdev->mem);
    g_free(hdev->mem_sections);

    memset(hdev, 0, sizeof(struct vhost_dev));
}

int vhost_dev_nvme_set_guest_notifier(struct vhost_dev *hdev,
                                      EventNotifier *notifier, uint32_t qid)
{
    struct vhost_vring_file file;

    file.fd = event_notifier_get_fd(notifier);
    file.index = qid;
    return hdev->vhost_ops->vhost_set_vring_call(hdev, &file);
}

