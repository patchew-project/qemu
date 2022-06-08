/*
 * generic functions used by VFIO devices
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "hw/vfio/vfio.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "hw/hw.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/range.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/migration.h"
#include "sysemu/tpm.h"

static QLIST_HEAD(, VFIOAddressSpace) vfio_address_spaces =
    QLIST_HEAD_INITIALIZER(vfio_address_spaces);

void vfio_host_win_add(VFIOContainer *container,
                       hwaddr min_iova, hwaddr max_iova,
                       uint64_t iova_pgsizes)
{
    VFIOHostDMAWindow *hostwin;

    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (ranges_overlap(hostwin->min_iova,
                           hostwin->max_iova - hostwin->min_iova + 1,
                           min_iova,
                           max_iova - min_iova + 1)) {
            hw_error("%s: Overlapped IOMMU are not enabled", __func__);
        }
    }

    hostwin = g_malloc0(sizeof(*hostwin));

    hostwin->min_iova = min_iova;
    hostwin->max_iova = max_iova;
    hostwin->iova_pgsizes = iova_pgsizes;
    QLIST_INSERT_HEAD(&container->hostwin_list, hostwin, hostwin_next);
}

int vfio_host_win_del(VFIOContainer *container, hwaddr min_iova,
                      hwaddr max_iova)
{
    VFIOHostDMAWindow *hostwin;

    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (hostwin->min_iova == min_iova && hostwin->max_iova == max_iova) {
            QLIST_REMOVE(hostwin, hostwin_next);
            g_free(hostwin);
            return 0;
        }
    }

    return -1;
}

static bool vfio_listener_skipped_section(MemoryRegionSection *section)
{
    return (!memory_region_is_ram(section->mr) &&
            !memory_region_is_iommu(section->mr)) ||
           memory_region_is_protected(section->mr) ||
           /*
            * Sizing an enabled 64-bit BAR can cause spurious mappings to
            * addresses in the upper part of the 64-bit address space.  These
            * are never accessed by the CPU and beyond the address width of
            * some IOMMU hardware.  TODO: VFIO should tell us the IOMMU width.
            */
           section->offset_within_address_space & (1ULL << 63);
}

/* Called with rcu_read_lock held.  */
static bool vfio_get_xlat_addr(IOMMUTLBEntry *iotlb, void **vaddr,
                               ram_addr_t *ram_addr, bool *read_only)
{
    MemoryRegion *mr;
    hwaddr xlat;
    hwaddr len = iotlb->addr_mask + 1;
    bool writable = iotlb->perm & IOMMU_WO;

    /*
     * The IOMMU TLB entry we have just covers translation through
     * this IOMMU to its immediate target.  We need to translate
     * it the rest of the way through to memory.
     */
    mr = address_space_translate(&address_space_memory,
                                 iotlb->translated_addr,
                                 &xlat, &len, writable,
                                 MEMTXATTRS_UNSPECIFIED);
    if (!memory_region_is_ram(mr)) {
        error_report("iommu map to non memory area %"HWADDR_PRIx"",
                     xlat);
        return false;
    } else if (memory_region_has_ram_discard_manager(mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(mr);
        MemoryRegionSection tmp = {
            .mr = mr,
            .offset_within_region = xlat,
            .size = int128_make64(len),
        };

        /*
         * Malicious VMs can map memory into the IOMMU, which is expected
         * to remain discarded. vfio will pin all pages, populating memory.
         * Disallow that. vmstate priorities make sure any RamDiscardManager
         * were already restored before IOMMUs are restored.
         */
        if (!ram_discard_manager_is_populated(rdm, &tmp)) {
            error_report("iommu map to discarded memory (e.g., unplugged via"
                         " virtio-mem): %"HWADDR_PRIx"",
                         iotlb->translated_addr);
            return false;
        }

        /*
         * Malicious VMs might trigger discarding of IOMMU-mapped memory. The
         * pages will remain pinned inside vfio until unmapped, resulting in a
         * higher memory consumption than expected. If memory would get
         * populated again later, there would be an inconsistency between pages
         * pinned by vfio and pages seen by QEMU. This is the case until
         * unmapped from the IOMMU (e.g., during device reset).
         *
         * With malicious guests, we really only care about pinning more memory
         * than expected. RLIMIT_MEMLOCK set for the user/process can never be
         * exceeded and can be used to mitigate this problem.
         */
        warn_report_once("Using vfio with vIOMMUs and coordinated discarding of"
                         " RAM (e.g., virtio-mem) works, however, malicious"
                         " guests can trigger pinning of more memory than"
                         " intended via an IOMMU. It's possible to mitigate "
                         " by setting/adjusting RLIMIT_MEMLOCK.");
    }

    /*
     * Translation truncates length to the IOMMU page size,
     * check that it did not truncate too much.
     */
    if (len & iotlb->addr_mask) {
        error_report("iommu has granularity incompatible with target AS");
        return false;
    }

    if (vaddr) {
        *vaddr = memory_region_get_ram_ptr(mr) + xlat;
    }

    if (ram_addr) {
        *ram_addr = memory_region_get_ram_addr(mr) + xlat;
    }

    if (read_only) {
        *read_only = !writable || mr->readonly;
    }

    return true;
}

static void vfio_iommu_map_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    VFIOGuestIOMMU *giommu = container_of(n, VFIOGuestIOMMU, n);
    VFIOContainer *container = giommu->container;
    hwaddr iova = iotlb->iova + giommu->iommu_offset;
    void *vaddr;
    int ret;

    trace_vfio_iommu_map_notify(iotlb->perm == IOMMU_NONE ? "UNMAP" : "MAP",
                                iova, iova + iotlb->addr_mask);

    if (iotlb->target_as != &address_space_memory) {
        error_report("Wrong target AS \"%s\", only system memory is allowed",
                     iotlb->target_as->name ? iotlb->target_as->name : "none");
        return;
    }

    rcu_read_lock();

    if ((iotlb->perm & IOMMU_RW) != IOMMU_NONE) {
        bool read_only;

        if (!vfio_get_xlat_addr(iotlb, &vaddr, NULL, &read_only)) {
            goto out;
        }
        /*
         * vaddr is only valid until rcu_read_unlock(). But after
         * vfio_dma_map has set up the mapping the pages will be
         * pinned by the kernel. This makes sure that the RAM backend
         * of vaddr will always be there, even if the memory object is
         * destroyed and its backing memory munmap-ed.
         */
        ret = vfio_container_dma_map(container, iova,
                                     iotlb->addr_mask + 1, vaddr,
                                     read_only);
        if (ret) {
            error_report("vfio_dma_map(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx", %p) = %d (%m)",
                         container, iova,
                         iotlb->addr_mask + 1, vaddr, ret);
        }
    } else {
        ret = vfio_container_dma_unmap(container, iova,
                                       iotlb->addr_mask + 1, iotlb);
        if (ret) {
            error_report("vfio_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx") = %d (%m)",
                         container, iova,
                         iotlb->addr_mask + 1, ret);
        }
    }
out:
    rcu_read_unlock();
}

static void vfio_ram_discard_notify_discard(RamDiscardListener *rdl,
                                            MemoryRegionSection *section)
{
    VFIORamDiscardListener *vrdl = container_of(rdl, VFIORamDiscardListener,
                                                listener);
    VFIOContainer *container = vrdl->container;
    const hwaddr size = int128_get64(section->size);
    const hwaddr iova = section->offset_within_address_space;
    int ret;

    /* Unmap with a single call. */
    ret = vfio_container_dma_unmap(container, iova, size , NULL);
    if (ret) {
        error_report("%s: vfio_dma_unmap() failed: %s", __func__,
                     strerror(-ret));
    }
}

static int vfio_ram_discard_notify_populate(RamDiscardListener *rdl,
                                            MemoryRegionSection *section)
{
    VFIORamDiscardListener *vrdl = container_of(rdl, VFIORamDiscardListener,
                                                listener);
    VFIOContainer *container = vrdl->container;
    const hwaddr end = section->offset_within_region +
                       int128_get64(section->size);
    hwaddr start, next, iova;
    void *vaddr;
    int ret;

    /*
     * Map in (aligned within memory region) minimum granularity, so we can
     * unmap in minimum granularity later.
     */
    for (start = section->offset_within_region; start < end; start = next) {
        next = ROUND_UP(start + 1, vrdl->granularity);
        next = MIN(next, end);

        iova = start - section->offset_within_region +
               section->offset_within_address_space;
        vaddr = memory_region_get_ram_ptr(section->mr) + start;

        ret = vfio_container_dma_map(container, iova, next - start,
                                     vaddr, section->readonly);
        if (ret) {
            /* Rollback */
            vfio_ram_discard_notify_discard(rdl, section);
            return ret;
        }
    }
    return 0;
}

static void vfio_register_ram_discard_listener(VFIOContainer *container,
                                               MemoryRegionSection *section)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    VFIORamDiscardListener *vrdl;

    /* Ignore some corner cases not relevant in practice. */
    g_assert(QEMU_IS_ALIGNED(section->offset_within_region, TARGET_PAGE_SIZE));
    g_assert(QEMU_IS_ALIGNED(section->offset_within_address_space,
                             TARGET_PAGE_SIZE));
    g_assert(QEMU_IS_ALIGNED(int128_get64(section->size), TARGET_PAGE_SIZE));

    vrdl = g_new0(VFIORamDiscardListener, 1);
    vrdl->container = container;
    vrdl->mr = section->mr;
    vrdl->offset_within_address_space = section->offset_within_address_space;
    vrdl->size = int128_get64(section->size);
    vrdl->granularity = ram_discard_manager_get_min_granularity(rdm,
                                                                section->mr);

    g_assert(vrdl->granularity && is_power_of_2(vrdl->granularity));
    g_assert(container->pgsizes &&
             vrdl->granularity >= 1ULL << ctz64(container->pgsizes));

    ram_discard_listener_init(&vrdl->listener,
                              vfio_ram_discard_notify_populate,
                              vfio_ram_discard_notify_discard, true);
    ram_discard_manager_register_listener(rdm, &vrdl->listener, section);
    QLIST_INSERT_HEAD(&container->vrdl_list, vrdl, next);

    /*
     * Sanity-check if we have a theoretically problematic setup where we could
     * exceed the maximum number of possible DMA mappings over time. We assume
     * that each mapped section in the same address space as a RamDiscardManager
     * section consumes exactly one DMA mapping, with the exception of
     * RamDiscardManager sections; i.e., we don't expect to have gIOMMU sections
     * in the same address space as RamDiscardManager sections.
     *
     * We assume that each section in the address space consumes one memslot.
     * We take the number of KVM memory slots as a best guess for the maximum
     * number of sections in the address space we could have over time,
     * also consuming DMA mappings.
     */
    if (container->dma_max_mappings) {
        unsigned int vrdl_count = 0, vrdl_mappings = 0, max_memslots = 512;

#ifdef CONFIG_KVM
        if (kvm_enabled()) {
            max_memslots = kvm_get_max_memslots();
        }
#endif

        QLIST_FOREACH(vrdl, &container->vrdl_list, next) {
            hwaddr start, end;

            start = QEMU_ALIGN_DOWN(vrdl->offset_within_address_space,
                                    vrdl->granularity);
            end = ROUND_UP(vrdl->offset_within_address_space + vrdl->size,
                           vrdl->granularity);
            vrdl_mappings += (end - start) / vrdl->granularity;
            vrdl_count++;
        }

        if (vrdl_mappings + max_memslots - vrdl_count >
            container->dma_max_mappings) {
            warn_report("%s: possibly running out of DMA mappings. E.g., try"
                        " increasing the 'block-size' of virtio-mem devies."
                        " Maximum possible DMA mappings: %d, Maximum possible"
                        " memslots: %d", __func__, container->dma_max_mappings,
                        max_memslots);
        }
    }
}

static void vfio_unregister_ram_discard_listener(VFIOContainer *container,
                                                 MemoryRegionSection *section)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    VFIORamDiscardListener *vrdl = NULL;

    QLIST_FOREACH(vrdl, &container->vrdl_list, next) {
        if (vrdl->mr == section->mr &&
            vrdl->offset_within_address_space ==
            section->offset_within_address_space) {
            break;
        }
    }

    if (!vrdl) {
        hw_error("vfio: Trying to unregister missing RAM discard listener");
    }

    ram_discard_manager_unregister_listener(rdm, &vrdl->listener);
    QLIST_REMOVE(vrdl, next);
    g_free(vrdl);
}

static bool vfio_known_safe_misalignment(MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    if (!TPM_IS_CRB(mr->owner)) {
        return false;
    }

    /* this is a known safe misaligned region, just trace for debug purpose */
    trace_vfio_known_safe_misalignment(memory_region_name(mr),
                                       section->offset_within_address_space,
                                       section->offset_within_region,
                                       qemu_real_host_page_size());
    return true;
}

static void vfio_listener_region_add(MemoryListener *listener,
                                     MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);
    hwaddr iova, end;
    Int128 llend, llsize;
    void *vaddr;
    int ret;
    VFIOHostDMAWindow *hostwin;
    bool hostwin_found;
    Error *err = NULL;

    if (vfio_listener_skipped_section(section)) {
        trace_vfio_listener_region_add_skip(
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return;
    }

    if (unlikely((section->offset_within_address_space &
                  ~qemu_real_host_page_mask()) !=
                 (section->offset_within_region & ~qemu_real_host_page_mask()))) {
        if (!vfio_known_safe_misalignment(section)) {
            error_report("%s received unaligned region %s iova=0x%"PRIx64
                         " offset_within_region=0x%"PRIx64
                         " qemu_real_host_page_size=0x%"PRIxPTR,
                         __func__, memory_region_name(section->mr),
                         section->offset_within_address_space,
                         section->offset_within_region,
                         qemu_real_host_page_size());
        }
        return;
    }

    iova = REAL_HOST_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(qemu_real_host_page_mask()));

    if (int128_ge(int128_make64(iova), llend)) {
        if (memory_region_is_ram_device(section->mr)) {
            trace_vfio_listener_region_add_no_dma_map(
                memory_region_name(section->mr),
                section->offset_within_address_space,
                int128_getlo(section->size),
                qemu_real_host_page_size());
        }
        return;
    }
    end = int128_get64(int128_sub(llend, int128_one()));

    if (vfio_container_add_section_window(container, section, &err)) {
        goto fail;
    }

    hostwin_found = false;
    QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
        if (hostwin->min_iova <= iova && end <= hostwin->max_iova) {
            hostwin_found = true;
            break;
        }
    }

    if (!hostwin_found) {
        error_setg(&err, "Container %p can't map guest IOVA region"
                   " 0x%"HWADDR_PRIx"..0x%"HWADDR_PRIx, container, iova, end);
        goto fail;
    }

    memory_region_ref(section->mr);

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;
        IOMMUMemoryRegion *iommu_mr = IOMMU_MEMORY_REGION(section->mr);
        int iommu_idx;

        trace_vfio_listener_region_add_iommu(iova, end);
        /*
         * FIXME: For VFIO iommu types which have KVM acceleration to
         * avoid bouncing all map/unmaps through qemu this way, this
         * would be the right place to wire that up (tell the KVM
         * device emulation the VFIO iommu handles to use).
         */
        giommu = g_malloc0(sizeof(*giommu));
        giommu->iommu_mr = iommu_mr;
        giommu->iommu_offset = section->offset_within_address_space -
                               section->offset_within_region;
        giommu->container = container;
        llend = int128_add(int128_make64(section->offset_within_region),
                           section->size);
        llend = int128_sub(llend, int128_one());
        iommu_idx = memory_region_iommu_attrs_to_index(iommu_mr,
                                                       MEMTXATTRS_UNSPECIFIED);
        iommu_notifier_init(&giommu->n, vfio_iommu_map_notify,
                            IOMMU_NOTIFIER_IOTLB_EVENTS,
                            section->offset_within_region,
                            int128_get64(llend),
                            iommu_idx);

        ret = memory_region_iommu_set_page_size_mask(giommu->iommu_mr,
                                                     container->pgsizes,
                                                     &err);
        if (ret) {
            g_free(giommu);
            goto fail;
        }

        ret = memory_region_register_iommu_notifier(section->mr, &giommu->n,
                                                    &err);
        if (ret) {
            g_free(giommu);
            goto fail;
        }
        QLIST_INSERT_HEAD(&container->giommu_list, giommu, giommu_next);
        memory_region_iommu_replay(giommu->iommu_mr, &giommu->n);

        return;
    }

    /* Here we assume that memory_region_is_ram(section->mr)==true */

    /*
     * For RAM memory regions with a RamDiscardManager, we only want to map the
     * actually populated parts - and update the mapping whenever we're notified
     * about changes.
     */
    if (memory_region_has_ram_discard_manager(section->mr)) {
        vfio_register_ram_discard_listener(container, section);
        return;
    }

    vaddr = memory_region_get_ram_ptr(section->mr) +
            section->offset_within_region +
            (iova - section->offset_within_address_space);

    trace_vfio_listener_region_add_ram(iova, end, vaddr);

    llsize = int128_sub(llend, int128_make64(iova));

    if (memory_region_is_ram_device(section->mr)) {
        hwaddr pgmask = (1ULL << ctz64(hostwin->iova_pgsizes)) - 1;

        if ((iova & pgmask) || (int128_get64(llsize) & pgmask)) {
            trace_vfio_listener_region_add_no_dma_map(
                memory_region_name(section->mr),
                section->offset_within_address_space,
                int128_getlo(section->size),
                pgmask + 1);
            return;
        }
    }

    ret = vfio_container_dma_map(container, iova, int128_get64(llsize),
                                 vaddr, section->readonly);
    if (ret) {
        error_setg(&err, "vfio_dma_map(%p, 0x%"HWADDR_PRIx", "
                   "0x%"HWADDR_PRIx", %p) = %d (%m)",
                   container, iova, int128_get64(llsize), vaddr, ret);
        if (memory_region_is_ram_device(section->mr)) {
            /* Allow unexpected mappings not to be fatal for RAM devices */
            error_report_err(err);
            return;
        }
        goto fail;
    }

    return;

fail:
    if (memory_region_is_ram_device(section->mr)) {
        error_report("failed to vfio_dma_map. pci p2p may not work");
        return;
    }
    /*
     * On the initfn path, store the first error in the container so we
     * can gracefully fail.  Runtime, there's not much we can do other
     * than throw a hardware error.
     */
    if (!container->initialized) {
        if (!container->error) {
            error_propagate_prepend(&container->error, err,
                                    "Region %s: ",
                                    memory_region_name(section->mr));
        } else {
            error_free(err);
        }
    } else {
        error_report_err(err);
        hw_error("vfio: DMA mapping failed, unable to continue");
    }
}

static void vfio_listener_region_del(MemoryListener *listener,
                                     MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);
    hwaddr iova, end;
    Int128 llend, llsize;
    int ret;
    bool try_unmap = true;

    if (vfio_listener_skipped_section(section)) {
        trace_vfio_listener_region_del_skip(
                section->offset_within_address_space,
                section->offset_within_address_space +
                int128_get64(int128_sub(section->size, int128_one())));
        return;
    }

    if (unlikely((section->offset_within_address_space &
                  ~qemu_real_host_page_mask()) !=
                 (section->offset_within_region & ~qemu_real_host_page_mask()))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;

        QLIST_FOREACH(giommu, &container->giommu_list, giommu_next) {
            if (MEMORY_REGION(giommu->iommu_mr) == section->mr &&
                giommu->n.start == section->offset_within_region) {
                memory_region_unregister_iommu_notifier(section->mr,
                                                        &giommu->n);
                QLIST_REMOVE(giommu, giommu_next);
                g_free(giommu);
                break;
            }
        }

        /*
         * FIXME: We assume the one big unmap below is adequate to
         * remove any individual page mappings in the IOMMU which
         * might have been copied into VFIO. This works for a page table
         * based IOMMU where a big unmap flattens a large range of IO-PTEs.
         * That may not be true for all IOMMU types.
         */
    }

    iova = REAL_HOST_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(qemu_real_host_page_mask()));

    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }
    end = int128_get64(int128_sub(llend, int128_one()));

    llsize = int128_sub(llend, int128_make64(iova));

    trace_vfio_listener_region_del(iova, end);

    if (memory_region_is_ram_device(section->mr)) {
        hwaddr pgmask;
        VFIOHostDMAWindow *hostwin;
        bool hostwin_found = false;

        QLIST_FOREACH(hostwin, &container->hostwin_list, hostwin_next) {
            if (hostwin->min_iova <= iova && end <= hostwin->max_iova) {
                hostwin_found = true;
                break;
            }
        }
        assert(hostwin_found); /* or region_add() would have failed */

        pgmask = (1ULL << ctz64(hostwin->iova_pgsizes)) - 1;
        try_unmap = !((iova & pgmask) || (int128_get64(llsize) & pgmask));
    } else if (memory_region_has_ram_discard_manager(section->mr)) {
        vfio_unregister_ram_discard_listener(container, section);
        /* Unregistering will trigger an unmap. */
        try_unmap = false;
    }

    if (try_unmap) {
        if (int128_eq(llsize, int128_2_64())) {
            /* The unmap ioctl doesn't accept a full 64-bit span. */
            llsize = int128_rshift(llsize, 1);
            ret = vfio_container_dma_unmap(container, iova,
                                           int128_get64(llsize), NULL);
            if (ret) {
                error_report("vfio_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                             "0x%"HWADDR_PRIx") = %d (%m)",
                             container, iova, int128_get64(llsize), ret);
            }
            iova += int128_get64(llsize);
        }
        ret = vfio_container_dma_unmap(container, iova,
                                       int128_get64(llsize), NULL);
        if (ret) {
            error_report("vfio_dma_unmap(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx") = %d (%m)",
                         container, iova, int128_get64(llsize), ret);
        }
    }

    memory_region_unref(section->mr);

    vfio_container_del_section_window(container, section);
}

static void vfio_listener_log_global_start(MemoryListener *listener)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);

    vfio_container_set_dirty_page_tracking(container, true);
}

static void vfio_listener_log_global_stop(MemoryListener *listener)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);

    vfio_container_set_dirty_page_tracking(container, false);
}

typedef struct {
    IOMMUNotifier n;
    VFIOGuestIOMMU *giommu;
} vfio_giommu_dirty_notifier;

static void vfio_iommu_map_dirty_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    vfio_giommu_dirty_notifier *gdn = container_of(n,
                                                vfio_giommu_dirty_notifier, n);
    VFIOGuestIOMMU *giommu = gdn->giommu;
    VFIOContainer *container = giommu->container;
    hwaddr iova = iotlb->iova + giommu->iommu_offset;
    ram_addr_t translated_addr;

    trace_vfio_iommu_map_dirty_notify(iova, iova + iotlb->addr_mask);

    if (iotlb->target_as != &address_space_memory) {
        error_report("Wrong target AS \"%s\", only system memory is allowed",
                     iotlb->target_as->name ? iotlb->target_as->name : "none");
        return;
    }

    rcu_read_lock();
    if (vfio_get_xlat_addr(iotlb, NULL, &translated_addr, NULL)) {
        int ret;

        ret = vfio_container_get_dirty_bitmap(container, iova,
                                              iotlb->addr_mask + 1,
                                              translated_addr);
        if (ret) {
            error_report("vfio_iommu_map_dirty_notify(%p, 0x%"HWADDR_PRIx", "
                         "0x%"HWADDR_PRIx") = %d (%m)",
                         container, iova,
                         iotlb->addr_mask + 1, ret);
        }
    }
    rcu_read_unlock();
}

static int vfio_ram_discard_get_dirty_bitmap(MemoryRegionSection *section,
                                             void *opaque)
{
    const hwaddr size = int128_get64(section->size);
    const hwaddr iova = section->offset_within_address_space;
    const ram_addr_t ram_addr = memory_region_get_ram_addr(section->mr) +
                                section->offset_within_region;
    VFIORamDiscardListener *vrdl = opaque;

    /*
     * Sync the whole mapped region (spanning multiple individual mappings)
     * in one go.
     */
    return vfio_container_get_dirty_bitmap(vrdl->container, iova,
                                           size, ram_addr);
}

static int
vfio_sync_ram_discard_listener_dirty_bitmap(VFIOContainer *container,
                                            MemoryRegionSection *section)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    VFIORamDiscardListener *vrdl = NULL;

    QLIST_FOREACH(vrdl, &container->vrdl_list, next) {
        if (vrdl->mr == section->mr &&
            vrdl->offset_within_address_space ==
            section->offset_within_address_space) {
            break;
        }
    }

    if (!vrdl) {
        hw_error("vfio: Trying to sync missing RAM discard listener");
    }

    /*
     * We only want/can synchronize the bitmap for actually mapped parts -
     * which correspond to populated parts. Replay all populated parts.
     */
    return ram_discard_manager_replay_populated(rdm, section,
                                              vfio_ram_discard_get_dirty_bitmap,
                                                &vrdl);
}

static int vfio_sync_dirty_bitmap(VFIOContainer *container,
                                  MemoryRegionSection *section)
{
    ram_addr_t ram_addr;

    if (memory_region_is_iommu(section->mr)) {
        VFIOGuestIOMMU *giommu;

        QLIST_FOREACH(giommu, &container->giommu_list, giommu_next) {
            if (MEMORY_REGION(giommu->iommu_mr) == section->mr &&
                giommu->n.start == section->offset_within_region) {
                Int128 llend;
                vfio_giommu_dirty_notifier gdn = { .giommu = giommu };
                int idx = memory_region_iommu_attrs_to_index(giommu->iommu_mr,
                                                       MEMTXATTRS_UNSPECIFIED);

                llend = int128_add(int128_make64(section->offset_within_region),
                                   section->size);
                llend = int128_sub(llend, int128_one());

                iommu_notifier_init(&gdn.n,
                                    vfio_iommu_map_dirty_notify,
                                    IOMMU_NOTIFIER_MAP,
                                    section->offset_within_region,
                                    int128_get64(llend),
                                    idx);
                memory_region_iommu_replay(giommu->iommu_mr, &gdn.n);
                break;
            }
        }
        return 0;
    } else if (memory_region_has_ram_discard_manager(section->mr)) {
        return vfio_sync_ram_discard_listener_dirty_bitmap(container, section);
    }

    ram_addr = memory_region_get_ram_addr(section->mr) +
               section->offset_within_region;

    return vfio_container_get_dirty_bitmap(container,
                   REAL_HOST_PAGE_ALIGN(section->offset_within_address_space),
                   int128_get64(section->size), ram_addr);
}

static void vfio_listener_log_sync(MemoryListener *listener,
        MemoryRegionSection *section)
{
    VFIOContainer *container = container_of(listener, VFIOContainer, listener);

    if (vfio_listener_skipped_section(section) ||
        !container->dirty_pages_supported) {
        return;
    }

    if (vfio_container_devices_all_dirty_tracking(container)) {
        vfio_sync_dirty_bitmap(container, section);
    }
}

const MemoryListener vfio_memory_listener = {
    .name = "vfio",
    .region_add = vfio_listener_region_add,
    .region_del = vfio_listener_region_del,
    .log_global_start = vfio_listener_log_global_start,
    .log_global_stop = vfio_listener_log_global_stop,
    .log_sync = vfio_listener_log_sync,
};

VFIOAddressSpace *vfio_get_address_space(AddressSpace *as)
{
    VFIOAddressSpace *space;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        if (space->as == as) {
            return space;
        }
    }

    /* No suitable VFIOAddressSpace, create a new one */
    space = g_malloc0(sizeof(*space));
    space->as = as;
    QLIST_INIT(&space->containers);

    QLIST_INSERT_HEAD(&vfio_address_spaces, space, list);

    return space;
}

void vfio_put_address_space(VFIOAddressSpace *space)
{
    if (QLIST_EMPTY(&space->containers)) {
        QLIST_REMOVE(space, list);
        g_free(space);
    }
}

static const VFIOContainerOps *
vfio_get_container_ops(VFIOIOMMUBackendType be)
{
    switch (be) {
    case VFIO_IOMMU_BACKEND_TYPE_LEGACY:
        return &legacy_container_ops;
    default:
        return NULL;
    }
}

int vfio_attach_device(VFIODevice *vbasedev, AddressSpace *as, Error **errp)
{
    const VFIOContainerOps *ops;

    ops = vfio_get_container_ops(VFIO_IOMMU_BACKEND_TYPE_LEGACY);
    if (!ops) {
        return -ENOENT;
    }
    return ops->attach_device(vbasedev, as, errp);
}

void vfio_detach_device(VFIODevice *vbasedev)
{
    if (!vbasedev->container) {
        return;
    }
    vbasedev->container->ops->detach_device(vbasedev);
}
