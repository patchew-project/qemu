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
#include "qemu/range.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/migration.h"

#ifdef CONFIG_KVM
/*
 * We have a single VFIO pseudo device per KVM VM.  Once created it lives
 * for the life of the VM.  Closing the file descriptor only drops our
 * reference to it and the device's reference to kvm.  Therefore once
 * initialized, this file descriptor is only released on QEMU exit and
 * we'll re-use it should another vfio device be attached before then.
 */
static int vfio_kvm_device_fd = -1;
#endif

VFIOGroupList vfio_group_list =
    QLIST_HEAD_INITIALIZER(vfio_group_list);

/*
 * Device state interfaces
 */

bool vfio_mig_active(void)
{
    VFIOGroup *group;
    VFIODevice *vbasedev;

    if (QLIST_EMPTY(&vfio_group_list)) {
        return false;
    }

    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            if (vbasedev->migration_blocker) {
                return false;
            }
        }
    }
    return true;
}

static bool vfio_devices_all_dirty_tracking(VFIOContainer *bcontainer)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);
    VFIOGroup *group;
    VFIODevice *vbasedev;
    MigrationState *ms = migrate_get_current();

    if (!migration_is_setup_or_active(ms->state)) {
        return false;
    }

    QLIST_FOREACH(group, &container->group_list, container_next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            VFIOMigration *migration = vbasedev->migration;

            if (!migration) {
                return false;
            }

            if ((vbasedev->pre_copy_dirty_page_tracking == ON_OFF_AUTO_OFF)
                && (migration->device_state & VFIO_DEVICE_STATE_V1_RUNNING)) {
                return false;
            }
        }
    }
    return true;
}

static bool vfio_devices_all_running_and_saving(VFIOLegacyContainer *container)
{
    VFIOGroup *group;
    VFIODevice *vbasedev;
    MigrationState *ms = migrate_get_current();

    if (!migration_is_setup_or_active(ms->state)) {
        return false;
    }

    QLIST_FOREACH(group, &container->group_list, container_next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            VFIOMigration *migration = vbasedev->migration;

            if (!migration) {
                return false;
            }

            if ((migration->device_state & VFIO_DEVICE_STATE_V1_SAVING) &&
                (migration->device_state & VFIO_DEVICE_STATE_V1_RUNNING)) {
                continue;
            } else {
                return false;
            }
        }
    }
    return true;
}

static int vfio_dma_unmap_bitmap(VFIOLegacyContainer *container,
                                 hwaddr iova, ram_addr_t size,
                                 IOMMUTLBEntry *iotlb)
{
    VFIOContainer *bcontainer = &container->bcontainer;
    struct vfio_iommu_type1_dma_unmap *unmap;
    struct vfio_bitmap *bitmap;
    uint64_t pages = REAL_HOST_PAGE_ALIGN(size) / qemu_real_host_page_size();
    int ret;

    unmap = g_malloc0(sizeof(*unmap) + sizeof(*bitmap));

    unmap->argsz = sizeof(*unmap) + sizeof(*bitmap);
    unmap->iova = iova;
    unmap->size = size;
    unmap->flags |= VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP;
    bitmap = (struct vfio_bitmap *)&unmap->data;

    /*
     * cpu_physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty. Hence set bitmap_pgsize
     * to qemu_real_host_page_size.
     */

    bitmap->pgsize = qemu_real_host_page_size();
    bitmap->size = ROUND_UP(pages, sizeof(__u64) * BITS_PER_BYTE) /
                   BITS_PER_BYTE;

    if (bitmap->size > bcontainer->max_dirty_bitmap_size) {
        error_report("UNMAP: Size of bitmap too big 0x%"PRIx64,
                     (uint64_t)bitmap->size);
        ret = -E2BIG;
        goto unmap_exit;
    }

    bitmap->data = g_try_malloc0(bitmap->size);
    if (!bitmap->data) {
        ret = -ENOMEM;
        goto unmap_exit;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, unmap);
    if (!ret) {
        cpu_physical_memory_set_dirty_lebitmap((unsigned long *)bitmap->data,
                iotlb->translated_addr, pages);
    } else {
        error_report("VFIO_UNMAP_DMA with DIRTY_BITMAP : %m");
    }

    g_free(bitmap->data);
unmap_exit:
    g_free(unmap);
    return ret;
}

/*
 * DMA - Mapping and unmapping for the "type1" IOMMU interface used on x86
 */
static int vfio_dma_unmap(VFIOContainer *bcontainer,
                          hwaddr iova, ram_addr_t size,
                          IOMMUTLBEntry *iotlb)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = iova,
        .size = size,
    };

    if (iotlb && bcontainer->dirty_pages_supported &&
        vfio_devices_all_running_and_saving(container)) {
        return vfio_dma_unmap_bitmap(container, iova, size, iotlb);
    }

    while (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        /*
         * The type1 backend has an off-by-one bug in the kernel (71a7d3d78e3c
         * v4.15) where an overflow in its wrap-around check prevents us from
         * unmapping the last page of the address space.  Test for the error
         * condition and re-try the unmap excluding the last page.  The
         * expectation is that we've never mapped the last page anyway and this
         * unmap request comes via vIOMMU support which also makes it unlikely
         * that this page is used.  This bug was introduced well after type1 v2
         * support was introduced, so we shouldn't need to test for v1.  A fix
         * is queued for kernel v5.0 so this workaround can be removed once
         * affected kernels are sufficiently deprecated.
         */
        if (errno == EINVAL && unmap.size && !(unmap.iova + unmap.size) &&
            container->iommu_type == VFIO_TYPE1v2_IOMMU) {
            trace_vfio_dma_unmap_overflow_workaround();
            unmap.size -= 1ULL << ctz64(bcontainer->pgsizes);
            continue;
        }
        error_report("VFIO_UNMAP_DMA failed: %s", strerror(errno));
        return -errno;
    }

    return 0;
}

static bool vfio_legacy_container_check_extension(VFIOContainer *bcontainer,
                                                  VFIOContainerFeature feat)
{
    switch (feat) {
    case VFIO_FEAT_LIVE_MIGRATION:
        return true;
    default:
        return false;
    };
}

static int vfio_dma_map(VFIOContainer *bcontainer, hwaddr iova,
                       ram_addr_t size, void *vaddr, bool readonly)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);
    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .flags = VFIO_DMA_MAP_FLAG_READ,
        .vaddr = (__u64)(uintptr_t)vaddr,
        .iova = iova,
        .size = size,
    };

    if (!readonly) {
        map.flags |= VFIO_DMA_MAP_FLAG_WRITE;
    }

    /*
     * Try the mapping, if it fails with EBUSY, unmap the region and try
     * again.  This shouldn't be necessary, but we sometimes see it in
     * the VGA ROM space.
     */
    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0 ||
        (errno == EBUSY && vfio_dma_unmap(bcontainer, iova, size, NULL) == 0 &&
         ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0)) {
        return 0;
    }

    error_report("VFIO_MAP_DMA failed: %s", strerror(errno));
    return -errno;
}

static void vfio_set_dirty_page_tracking(VFIOContainer *bcontainer, bool start)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);
    int ret;
    struct vfio_iommu_type1_dirty_bitmap dirty = {
        .argsz = sizeof(dirty),
    };

    if (start) {
        dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_START;
    } else {
        dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_DIRTY_PAGES, &dirty);
    if (ret) {
        error_report("Failed to set dirty tracking flag 0x%x errno: %d",
                     dirty.flags, errno);
    }
}

static int vfio_get_dirty_bitmap(VFIOContainer *bcontainer, uint64_t iova,
                                 uint64_t size, ram_addr_t ram_addr)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);
    struct vfio_iommu_type1_dirty_bitmap *dbitmap;
    struct vfio_iommu_type1_dirty_bitmap_get *range;
    uint64_t pages;
    int ret;

    dbitmap = g_malloc0(sizeof(*dbitmap) + sizeof(*range));

    dbitmap->argsz = sizeof(*dbitmap) + sizeof(*range);
    dbitmap->flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP;
    range = (struct vfio_iommu_type1_dirty_bitmap_get *)&dbitmap->data;
    range->iova = iova;
    range->size = size;

    /*
     * cpu_physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty. Hence set bitmap's pgsize
     * to qemu_real_host_page_size.
     */
    range->bitmap.pgsize = qemu_real_host_page_size();

    pages = REAL_HOST_PAGE_ALIGN(range->size) / qemu_real_host_page_size();
    range->bitmap.size = ROUND_UP(pages, sizeof(__u64) * BITS_PER_BYTE) /
                                         BITS_PER_BYTE;
    range->bitmap.data = g_try_malloc0(range->bitmap.size);
    if (!range->bitmap.data) {
        ret = -ENOMEM;
        goto err_out;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_DIRTY_PAGES, dbitmap);
    if (ret) {
        error_report("Failed to get dirty bitmap for iova: 0x%"PRIx64
                " size: 0x%"PRIx64" err: %d", (uint64_t)range->iova,
                (uint64_t)range->size, errno);
        goto err_out;
    }

    cpu_physical_memory_set_dirty_lebitmap((unsigned long *)range->bitmap.data,
                                            ram_addr, pages);

    trace_vfio_get_dirty_bitmap(container->fd, range->iova, range->size,
                                range->bitmap.size, ram_addr);
err_out:
    g_free(range->bitmap.data);
    g_free(dbitmap);

    return ret;
}

static void vfio_listener_release(VFIOLegacyContainer *container)
{
    VFIOContainer *bcontainer = &container->bcontainer;

    memory_listener_unregister(&bcontainer->listener);
    if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
        memory_listener_unregister(&container->prereg_listener);
    }
}

static int
vfio_legacy_container_add_section_window(VFIOContainer *bcontainer,
                                         MemoryRegionSection *section,
                                         Error **errp)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);
    VFIOHostDMAWindow *hostwin;
    hwaddr pgsize = 0;
    int ret;

    if (container->iommu_type != VFIO_SPAPR_TCE_v2_IOMMU) {
        return 0;
    }

    /* For now intersections are not allowed, we may relax this later */
    QLIST_FOREACH(hostwin, &bcontainer->hostwin_list, hostwin_next) {
        if (ranges_overlap(hostwin->min_iova,
                           hostwin->max_iova - hostwin->min_iova + 1,
                           section->offset_within_address_space,
                           int128_get64(section->size))) {
            error_setg(errp,
                    "region [0x%"PRIx64",0x%"PRIx64"] overlaps with existing"
                    "host DMA window [0x%"PRIx64",0x%"PRIx64"]",
                    section->offset_within_address_space,
                    section->offset_within_address_space +
                        int128_get64(section->size) - 1,
                    hostwin->min_iova, hostwin->max_iova);
            return -1;
        }
    }

    ret = vfio_spapr_create_window(container, section, &pgsize);
    if (ret) {
        error_setg_errno(errp, -ret, "Failed to create SPAPR window");
        return ret;
    }

    vfio_host_win_add(bcontainer, section->offset_within_address_space,
                      section->offset_within_address_space +
                      int128_get64(section->size) - 1, pgsize);
#ifdef CONFIG_KVM
    if (kvm_enabled()) {
        VFIOGroup *group;
        IOMMUMemoryRegion *iommu_mr = IOMMU_MEMORY_REGION(section->mr);
        struct kvm_vfio_spapr_tce param;
        struct kvm_device_attr attr = {
                .group = KVM_DEV_VFIO_GROUP,
                .attr = KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE,
                .addr = (uint64_t)(unsigned long)&param,
        };

        if (!memory_region_iommu_get_attr(iommu_mr, IOMMU_ATTR_SPAPR_TCE_FD,
                                          &param.tablefd)) {
            QLIST_FOREACH(group, &container->group_list, container_next) {
                param.groupfd = group->fd;
                if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
                    error_report("vfio: failed to setup fd %d "
                                 "for a group with fd %d: %s",
                                 param.tablefd, param.groupfd,
                                 strerror(errno));
                    return -1;
                }
                trace_vfio_spapr_group_attach(param.groupfd, param.tablefd);
            }
        }
    }
#endif
    return 0;
}

static void
vfio_legacy_container_del_section_window(VFIOContainer *bcontainer,
                                         MemoryRegionSection *section)
{
    VFIOLegacyContainer *container = container_of(bcontainer,
                                                  VFIOLegacyContainer,
                                                  bcontainer);

    if (container->iommu_type != VFIO_SPAPR_TCE_v2_IOMMU) {
        return;
    }

    vfio_spapr_remove_window(container,
                             section->offset_within_address_space);
    if (vfio_host_win_del(bcontainer,
                          section->offset_within_address_space,
                          section->offset_within_address_space +
                          int128_get64(section->size) - 1) < 0) {
        hw_error("%s: Cannot delete missing window at %"HWADDR_PRIx,
                 __func__, section->offset_within_address_space);
    }
}

void vfio_reset_handler(void *opaque)
{
    VFIOGroup *group;
    VFIODevice *vbasedev;

    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            if (vbasedev->dev->realized) {
                vbasedev->ops->vfio_compute_needs_reset(vbasedev);
            }
        }
    }

    QLIST_FOREACH(group, &vfio_group_list, next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            if (vbasedev->dev->realized && vbasedev->needs_reset) {
                vbasedev->ops->vfio_hot_reset_multi(vbasedev);
            }
        }
    }
}

static void vfio_kvm_device_add_group(VFIOGroup *group)
{
#ifdef CONFIG_KVM
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_ADD,
        .addr = (uint64_t)(unsigned long)&group->fd,
    };

    if (!kvm_enabled()) {
        return;
    }

    if (vfio_kvm_device_fd < 0) {
        struct kvm_create_device cd = {
            .type = KVM_DEV_TYPE_VFIO,
        };

        if (kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd)) {
            error_report("Failed to create KVM VFIO device: %m");
            return;
        }

        vfio_kvm_device_fd = cd.fd;
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_report("Failed to add group %d to KVM VFIO device: %m",
                     group->groupid);
    }
#endif
}

static void vfio_kvm_device_del_group(VFIOGroup *group)
{
#ifdef CONFIG_KVM
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_DEL,
        .addr = (uint64_t)(unsigned long)&group->fd,
    };

    if (vfio_kvm_device_fd < 0) {
        return;
    }

    if (ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr)) {
        error_report("Failed to remove group %d from KVM VFIO device: %m",
                     group->groupid);
    }
#endif
}

/*
 * vfio_get_iommu_type - selects the richest iommu_type (v2 first)
 */
static int vfio_get_iommu_type(VFIOLegacyContainer *container,
                               Error **errp)
{
    int iommu_types[] = { VFIO_TYPE1v2_IOMMU, VFIO_TYPE1_IOMMU,
                          VFIO_SPAPR_TCE_v2_IOMMU, VFIO_SPAPR_TCE_IOMMU };
    int i;

    for (i = 0; i < ARRAY_SIZE(iommu_types); i++) {
        if (ioctl(container->fd, VFIO_CHECK_EXTENSION, iommu_types[i])) {
            return iommu_types[i];
        }
    }
    error_setg(errp, "No available IOMMU models");
    return -EINVAL;
}

static int vfio_init_container(VFIOLegacyContainer *container, int group_fd,
                               Error **errp)
{
    int iommu_type, ret;

    iommu_type = vfio_get_iommu_type(container, errp);
    if (iommu_type < 0) {
        return iommu_type;
    }

    ret = ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container->fd);
    if (ret) {
        error_setg_errno(errp, errno, "Failed to set group container");
        return -errno;
    }

    while (ioctl(container->fd, VFIO_SET_IOMMU, iommu_type)) {
        if (iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
            /*
             * On sPAPR, despite the IOMMU subdriver always advertises v1 and
             * v2, the running platform may not support v2 and there is no
             * way to guess it until an IOMMU group gets added to the container.
             * So in case it fails with v2, try v1 as a fallback.
             */
            iommu_type = VFIO_SPAPR_TCE_IOMMU;
            continue;
        }
        error_setg_errno(errp, errno, "Failed to set iommu for container");
        return -errno;
    }

    container->iommu_type = iommu_type;
    return 0;
}

static int vfio_get_iommu_info(VFIOLegacyContainer *container,
                               struct vfio_iommu_type1_info **info)
{

    size_t argsz = sizeof(struct vfio_iommu_type1_info);

    *info = g_new0(struct vfio_iommu_type1_info, 1);
again:
    (*info)->argsz = argsz;

    if (ioctl(container->fd, VFIO_IOMMU_GET_INFO, *info)) {
        g_free(*info);
        *info = NULL;
        return -errno;
    }

    if (((*info)->argsz > argsz)) {
        argsz = (*info)->argsz;
        *info = g_realloc(*info, argsz);
        goto again;
    }

    return 0;
}

static struct vfio_info_cap_header *
vfio_get_iommu_info_cap(struct vfio_iommu_type1_info *info, uint16_t id)
{
    struct vfio_info_cap_header *hdr;
    void *ptr = info;

    if (!(info->flags & VFIO_IOMMU_INFO_CAPS)) {
        return NULL;
    }

    for (hdr = ptr + info->cap_offset; hdr != ptr; hdr = ptr + hdr->next) {
        if (hdr->id == id) {
            return hdr;
        }
    }

    return NULL;
}

static void vfio_get_iommu_info_migration(VFIOLegacyContainer *container,
                                          struct vfio_iommu_type1_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_iommu_type1_info_cap_migration *cap_mig;
    VFIOContainer *bcontainer = &container->bcontainer;

    hdr = vfio_get_iommu_info_cap(info, VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION);
    if (!hdr) {
        return;
    }

    cap_mig = container_of(hdr, struct vfio_iommu_type1_info_cap_migration,
                            header);

    /*
     * cpu_physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty.
     */
    if (cap_mig->pgsize_bitmap & qemu_real_host_page_size()) {
        bcontainer->dirty_pages_supported = true;
        bcontainer->max_dirty_bitmap_size = cap_mig->max_dirty_bitmap_size;
        bcontainer->dirty_pgsizes = cap_mig->pgsize_bitmap;
    }
}

static int
vfio_ram_block_discard_disable(VFIOLegacyContainer *container, bool state)
{
    switch (container->iommu_type) {
    case VFIO_TYPE1v2_IOMMU:
    case VFIO_TYPE1_IOMMU:
        /*
         * We support coordinated discarding of RAM via the RamDiscardManager.
         */
        return ram_block_uncoordinated_discard_disable(state);
    default:
        /*
         * VFIO_SPAPR_TCE_IOMMU most probably works just fine with
         * RamDiscardManager, however, it is completely untested.
         *
         * VFIO_SPAPR_TCE_v2_IOMMU with "DMA memory preregistering" does
         * completely the opposite of managing mapping/pinning dynamically as
         * required by RamDiscardManager. We would have to special-case sections
         * with a RamDiscardManager.
         */
        return ram_block_discard_disable(state);
    }
}

static int vfio_connect_container(VFIOGroup *group, AddressSpace *as,
                                  Error **errp)
{
    VFIOContainer *bcontainer;
    VFIOLegacyContainer *container;
    int ret, fd;
    VFIOAddressSpace *space;

    space = vfio_get_address_space(as);

    /*
     * VFIO is currently incompatible with discarding of RAM insofar as the
     * madvise to purge (zap) the page from QEMU's address space does not
     * interact with the memory API and therefore leaves stale virtual to
     * physical mappings in the IOMMU if the page was previously pinned.  We
     * therefore set discarding broken for each group added to a container,
     * whether the container is used individually or shared.  This provides
     * us with options to allow devices within a group to opt-in and allow
     * discarding, so long as it is done consistently for a group (for instance
     * if the device is an mdev device where it is known that the host vendor
     * driver will never pin pages outside of the working set of the guest
     * driver, which would thus not be discarding candidates).
     *
     * The first opportunity to induce pinning occurs here where we attempt to
     * attach the group to existing containers within the AddressSpace.  If any
     * pages are already zapped from the virtual address space, such as from
     * previous discards, new pinning will cause valid mappings to be
     * re-established.  Likewise, when the overall MemoryListener for a new
     * container is registered, a replay of mappings within the AddressSpace
     * will occur, re-establishing any previously zapped pages as well.
     *
     * Especially virtio-balloon is currently only prevented from discarding
     * new memory, it will not yet set ram_block_discard_set_required() and
     * therefore, neither stops us here or deals with the sudden memory
     * consumption of inflated memory.
     *
     * We do support discarding of memory coordinated via the RamDiscardManager
     * with some IOMMU types. vfio_ram_block_discard_disable() handles the
     * details once we know which type of IOMMU we are using.
     */

    QLIST_FOREACH(bcontainer, &space->containers, next) {
        container = container_of(bcontainer, VFIOLegacyContainer, bcontainer);
        if (!ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &container->fd)) {
            ret = vfio_ram_block_discard_disable(container, true);
            if (ret) {
                error_setg_errno(errp, -ret,
                                 "Cannot set discarding of RAM broken");
                if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER,
                          &container->fd)) {
                    error_report("vfio: error disconnecting group %d from"
                                 " container", group->groupid);
                }
                return ret;
            }
            group->container = container;
            QLIST_INSERT_HEAD(&container->group_list, group, container_next);
            vfio_kvm_device_add_group(group);
            return 0;
        }
    }

    fd = qemu_open_old("/dev/vfio/vfio", O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "failed to open /dev/vfio/vfio");
        ret = -errno;
        goto put_space_exit;
    }

    ret = ioctl(fd, VFIO_GET_API_VERSION);
    if (ret != VFIO_API_VERSION) {
        error_setg(errp, "supported vfio version: %d, "
                   "reported version: %d", VFIO_API_VERSION, ret);
        ret = -EINVAL;
        goto close_fd_exit;
    }

    container = g_malloc0(sizeof(*container));
    container->fd = fd;
    bcontainer = &container->bcontainer;
    vfio_container_init(bcontainer, space, &legacy_container_ops);

    ret = vfio_init_container(container, group->fd, errp);
    if (ret) {
        goto free_container_exit;
    }

    ret = vfio_ram_block_discard_disable(container, true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto free_container_exit;
    }

    switch (container->iommu_type) {
    case VFIO_TYPE1v2_IOMMU:
    case VFIO_TYPE1_IOMMU:
    {
        struct vfio_iommu_type1_info *info;

        /*
         * FIXME: This assumes that a Type1 IOMMU can map any 64-bit
         * IOVA whatsoever.  That's not actually true, but the current
         * kernel interface doesn't tell us what it can map, and the
         * existing Type1 IOMMUs generally support any IOVA we're
         * going to actually try in practice.
         */
        ret = vfio_get_iommu_info(container, &info);

        if (ret || !(info->flags & VFIO_IOMMU_INFO_PGSIZES)) {
            /* Assume 4k IOVA page size */
            info->iova_pgsizes = 4096;
        }
        vfio_host_win_add(bcontainer, 0, (hwaddr)-1, info->iova_pgsizes);
        bcontainer->pgsizes = info->iova_pgsizes;

        /* The default in the kernel ("dma_entry_limit") is 65535. */
        bcontainer->dma_max_mappings = 65535;
        if (!ret) {
            vfio_get_info_dma_avail(info, &bcontainer->dma_max_mappings);
            vfio_get_iommu_info_migration(container, info);
        }
        g_free(info);
        break;
    }
    case VFIO_SPAPR_TCE_v2_IOMMU:
    case VFIO_SPAPR_TCE_IOMMU:
    {
        struct vfio_iommu_spapr_tce_info info;
        bool v2 = container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU;

        /*
         * The host kernel code implementing VFIO_IOMMU_DISABLE is called
         * when container fd is closed so we do not call it explicitly
         * in this file.
         */
        if (!v2) {
            ret = ioctl(fd, VFIO_IOMMU_ENABLE);
            if (ret) {
                error_setg_errno(errp, errno, "failed to enable container");
                ret = -errno;
                goto enable_discards_exit;
            }
        } else {
            container->prereg_listener = vfio_prereg_listener;

            memory_listener_register(&container->prereg_listener,
                                     &address_space_memory);
            if (bcontainer->error) {
                memory_listener_unregister(&container->prereg_listener);
                ret = -1;
                error_propagate_prepend(errp, bcontainer->error,
                    "RAM memory listener initialization failed: ");
                goto enable_discards_exit;
            }
        }

        info.argsz = sizeof(info);
        ret = ioctl(fd, VFIO_IOMMU_SPAPR_TCE_GET_INFO, &info);
        if (ret) {
            error_setg_errno(errp, errno,
                             "VFIO_IOMMU_SPAPR_TCE_GET_INFO failed");
            ret = -errno;
            if (v2) {
                memory_listener_unregister(&container->prereg_listener);
            }
            goto enable_discards_exit;
        }

        if (v2) {
            bcontainer->pgsizes = info.ddw.pgsizes;
            /*
             * There is a default window in just created container.
             * To make region_add/del simpler, we better remove this
             * window now and let those iommu_listener callbacks
             * create/remove them when needed.
             */
            ret = vfio_spapr_remove_window(container, info.dma32_window_start);
            if (ret) {
                error_setg_errno(errp, -ret,
                                 "failed to remove existing window");
                goto enable_discards_exit;
            }
        } else {
            /* The default table uses 4K pages */
            bcontainer->pgsizes = 0x1000;
            vfio_host_win_add(bcontainer, info.dma32_window_start,
                              info.dma32_window_start +
                              info.dma32_window_size - 1,
                              0x1000);
        }
    }
    }

    vfio_kvm_device_add_group(group);

    QLIST_INIT(&container->group_list);
    QLIST_INSERT_HEAD(&space->containers, bcontainer, next);

    group->container = container;
    QLIST_INSERT_HEAD(&container->group_list, group, container_next);

    bcontainer->listener = vfio_memory_listener;

    memory_listener_register(&bcontainer->listener, bcontainer->space->as);

    if (bcontainer->error) {
        ret = -1;
        error_propagate_prepend(errp, bcontainer->error,
            "memory listener initialization failed: ");
        goto listener_release_exit;
    }

    bcontainer->initialized = true;

    return 0;
listener_release_exit:
    QLIST_REMOVE(group, container_next);
    QLIST_REMOVE(bcontainer, next);
    vfio_kvm_device_del_group(group);
    vfio_listener_release(container);

enable_discards_exit:
    vfio_ram_block_discard_disable(container, false);

free_container_exit:
    g_free(container);

close_fd_exit:
    close(fd);

put_space_exit:
    vfio_put_address_space(space);

    return ret;
}

static void vfio_disconnect_container(VFIOGroup *group)
{
    VFIOLegacyContainer *container = group->container;
    VFIOContainer *bcontainer = &container->bcontainer;

    QLIST_REMOVE(group, container_next);
    group->container = NULL;

    /*
     * Explicitly release the listener first before unset container,
     * since unset may destroy the backend container if it's the last
     * group.
     */
    if (QLIST_EMPTY(&container->group_list)) {
        vfio_listener_release(container);
    }

    if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd)) {
        error_report("vfio: error disconnecting group %d from container",
                     group->groupid);
    }

    if (QLIST_EMPTY(&container->group_list)) {
        VFIOAddressSpace *space = bcontainer->space;

        vfio_container_destroy(bcontainer);
        trace_vfio_disconnect_container(container->fd);
        close(container->fd);
        g_free(container);

        vfio_put_address_space(space);
    }
}

static VFIOGroup *vfio_get_group(int groupid, AddressSpace *as, Error **errp)
{
    VFIOGroup *group;
    VFIOContainer *bcontainer;
    char path[32];
    struct vfio_group_status status = { .argsz = sizeof(status) };

    QLIST_FOREACH(group, &vfio_group_list, next) {
        if (group->groupid == groupid) {
            /* Found it.  Now is it already in the right context? */
            bcontainer = &group->container->bcontainer;
            if (bcontainer->space->as == as) {
                return group;
            } else {
                error_setg(errp, "group %d used in multiple address spaces",
                           group->groupid);
                return NULL;
            }
        }
    }

    group = g_malloc0(sizeof(*group));

    snprintf(path, sizeof(path), "/dev/vfio/%d", groupid);
    group->fd = qemu_open_old(path, O_RDWR);
    if (group->fd < 0) {
        error_setg_errno(errp, errno, "failed to open %s", path);
        goto free_group_exit;
    }

    if (ioctl(group->fd, VFIO_GROUP_GET_STATUS, &status)) {
        error_setg_errno(errp, errno, "failed to get group %d status", groupid);
        goto close_fd_exit;
    }

    if (!(status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        error_setg(errp, "group %d is not viable", groupid);
        error_append_hint(errp,
                          "Please ensure all devices within the iommu_group "
                          "are bound to their vfio bus driver.\n");
        goto close_fd_exit;
    }

    group->groupid = groupid;
    QLIST_INIT(&group->device_list);

    if (vfio_connect_container(group, as, errp)) {
        error_prepend(errp, "failed to setup container for group %d: ",
                      groupid);
        goto close_fd_exit;
    }

    if (QLIST_EMPTY(&vfio_group_list)) {
        qemu_register_reset(vfio_reset_handler, NULL);
    }

    QLIST_INSERT_HEAD(&vfio_group_list, group, next);

    return group;

close_fd_exit:
    close(group->fd);

free_group_exit:
    g_free(group);

    return NULL;
}

static void vfio_put_group(VFIOGroup *group)
{
    if (!group || !QLIST_EMPTY(&group->device_list)) {
        return;
    }

    if (!group->ram_block_discard_allowed) {
        vfio_ram_block_discard_disable(group->container, false);
    }
    vfio_kvm_device_del_group(group);
    vfio_disconnect_container(group);
    QLIST_REMOVE(group, next);
    trace_vfio_put_group(group->fd);
    close(group->fd);
    g_free(group);

    if (QLIST_EMPTY(&vfio_group_list)) {
        qemu_unregister_reset(vfio_reset_handler, NULL);
    }
}

static int vfio_get_device(VFIOGroup *group, const char *name,
                           VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_info dev_info = { .argsz = sizeof(dev_info) };
    int ret, fd;

    fd = ioctl(group->fd, VFIO_GROUP_GET_DEVICE_FD, name);
    if (fd < 0) {
        error_setg_errno(errp, errno, "error getting device from group %d",
                         group->groupid);
        error_append_hint(errp,
                      "Verify all devices in group %d are bound to vfio-<bus> "
                      "or pci-stub and not already in use\n", group->groupid);
        return fd;
    }

    ret = ioctl(fd, VFIO_DEVICE_GET_INFO, &dev_info);
    if (ret) {
        error_setg_errno(errp, errno, "error getting device info");
        close(fd);
        return ret;
    }

    /*
     * Set discarding of RAM as not broken for this group if the driver knows
     * the device operates compatibly with discarding.  Setting must be
     * consistent per group, but since compatibility is really only possible
     * with mdev currently, we expect singleton groups.
     */
    if (vbasedev->ram_block_discard_allowed !=
        group->ram_block_discard_allowed) {
        if (!QLIST_EMPTY(&group->device_list)) {
            error_setg(errp, "Inconsistent setting of support for discarding "
                       "RAM (e.g., balloon) within group");
            close(fd);
            return -1;
        }

        if (!group->ram_block_discard_allowed) {
            group->ram_block_discard_allowed = true;
            vfio_ram_block_discard_disable(group->container, false);
        }
    }

    vbasedev->fd = fd;
    vbasedev->group = group;
    QLIST_INSERT_HEAD(&group->device_list, vbasedev, next);

    vbasedev->num_irqs = dev_info.num_irqs;
    vbasedev->num_regions = dev_info.num_regions;
    vbasedev->flags = dev_info.flags;

    trace_vfio_get_device(name, dev_info.flags, dev_info.num_regions,
                          dev_info.num_irqs);

    vbasedev->reset_works = !!(dev_info.flags & VFIO_DEVICE_FLAGS_RESET);
    return 0;
}

void vfio_put_base_device(VFIODevice *vbasedev)
{
    if (!vbasedev->group) {
        return;
    }
    QLIST_REMOVE(vbasedev, next);
    vbasedev->group = NULL;
    trace_vfio_put_base_device(vbasedev->fd);
    close(vbasedev->fd);
}

/* FIXME: should below code be in common.c? */
/*
 * Interfaces for IBM EEH (Enhanced Error Handling)
 */
static bool vfio_eeh_container_ok(VFIOLegacyContainer *container)
{
    /*
     * As of 2016-03-04 (linux-4.5) the host kernel EEH/VFIO
     * implementation is broken if there are multiple groups in a
     * container.  The hardware works in units of Partitionable
     * Endpoints (== IOMMU groups) and the EEH operations naively
     * iterate across all groups in the container, without any logic
     * to make sure the groups have their state synchronized.  For
     * certain operations (ENABLE) that might be ok, until an error
     * occurs, but for others (GET_STATE) it's clearly broken.
     */

    /*
     * XXX Once fixed kernels exist, test for them here
     */

    if (QLIST_EMPTY(&container->group_list)) {
        return false;
    }

    if (QLIST_NEXT(QLIST_FIRST(&container->group_list), container_next)) {
        return false;
    }

    return true;
}

static int vfio_eeh_container_op(VFIOLegacyContainer *container, uint32_t op)
{
    struct vfio_eeh_pe_op pe_op = {
        .argsz = sizeof(pe_op),
        .op = op,
    };
    int ret;

    if (!vfio_eeh_container_ok(container)) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x: "
                     "kernel requires a container with exactly one group", op);
        return -EPERM;
    }

    ret = ioctl(container->fd, VFIO_EEH_PE_OP, &pe_op);
    if (ret < 0) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x failed: %m", op);
        return -errno;
    }

    return ret;
}

static VFIOLegacyContainer *vfio_eeh_as_container(AddressSpace *as)
{
    VFIOAddressSpace *space = vfio_get_address_space(as);
    VFIOLegacyContainer *container = NULL;
    VFIOContainer *bcontainer = NULL;

    if (QLIST_EMPTY(&space->containers)) {
        /* No containers to act on */
        goto out;
    }

    bcontainer = QLIST_FIRST(&space->containers);
    container = container_of(bcontainer, VFIOLegacyContainer, bcontainer);

    if (QLIST_NEXT(bcontainer, next)) {
        /*
         * We don't yet have logic to synchronize EEH state across
         * multiple containers.
         */
        container = NULL;
        goto out;
    }

out:
    vfio_put_address_space(space);
    return container;
}

bool vfio_eeh_as_ok(AddressSpace *as)
{
    VFIOLegacyContainer *container = vfio_eeh_as_container(as);

    return (container != NULL) && vfio_eeh_container_ok(container);
}

int vfio_eeh_as_op(AddressSpace *as, uint32_t op)
{
    VFIOLegacyContainer *container = vfio_eeh_as_container(as);

    if (!container) {
        return -ENODEV;
    }
    return vfio_eeh_container_op(container, op);
}

static int vfio_device_groupid(VFIODevice *vbasedev, Error **errp)
{
    char *tmp, group_path[PATH_MAX], *group_name;
    int ret, groupid;
    ssize_t len;

    tmp = g_strdup_printf("%s/iommu_group", vbasedev->sysfsdev);
    len = readlink(tmp, group_path, sizeof(group_path));
    g_free(tmp);

    if (len <= 0 || len >= sizeof(group_path)) {
        ret = len < 0 ? -errno : -ENAMETOOLONG;
        error_setg_errno(errp, -ret, "no iommu_group found");
        return ret;
    }

    group_path[len] = 0;

    group_name = basename(group_path);
    if (sscanf(group_name, "%d", &groupid) != 1) {
        error_setg_errno(errp, errno, "failed to read %s", group_path);
        return -errno;
    }
    return groupid;
}

static int
legacy_attach_device(VFIODevice *vbasedev, AddressSpace *as, Error **errp)
{
    int groupid = vfio_device_groupid(vbasedev, errp);
    VFIODevice *vbasedev_iter;
    VFIOGroup *group;
    int ret;

    if (groupid < 0) {
        return groupid;
    }

    trace_vfio_realize(vbasedev->name, groupid);
    group = vfio_get_group(groupid, as, errp);
    if (!group) {
        return -1;
    }

    QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
        if (strcmp(vbasedev_iter->name, vbasedev->name) == 0) {
            error_setg(errp, "device is already attached");
            vfio_put_group(group);
            return -1;
        }
    }
    ret = vfio_get_device(group, vbasedev->name, vbasedev, errp);
    if (ret) {
        vfio_put_group(group);
        return -1;
    }
    vbasedev->container = &group->container->bcontainer;

    return 0;
}

static void legacy_detach_device(VFIODevice *vbasedev)
{
    vfio_put_base_device(vbasedev);
    vfio_put_group(vbasedev->group);
    vbasedev->container = NULL;
}

const VFIOContainerOps legacy_container_ops = {
    .dma_map = vfio_dma_map,
    .dma_unmap = vfio_dma_unmap,
    .devices_all_dirty_tracking = vfio_devices_all_dirty_tracking,
    .set_dirty_page_tracking = vfio_set_dirty_page_tracking,
    .get_dirty_bitmap = vfio_get_dirty_bitmap,
    .add_window = vfio_legacy_container_add_section_window,
    .del_window = vfio_legacy_container_del_section_window,
    .check_extension = vfio_legacy_container_check_extension,
    .attach_device = legacy_attach_device,
    .detach_device = legacy_detach_device,
};
