/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Apple/macOS VFIO IOMMU container backend.
 *
 * Copyright (c) 2026 Scott J. Goldman
 */

#include "qemu/osdep.h"

#include <linux/vfio.h>

#include "apple-dext-client.h"
#include "hw/vfio/apple.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/vfio-listener.h"
#include "qapi/error.h"
#include "system/ramblock.h"

static bool apple_vfio_setup(VFIOContainer *bcontainer, Error **errp)
{
    bcontainer->pgsizes = qemu_real_host_page_size();
    bcontainer->dma_max_mappings = UINT_MAX;
    bcontainer->dirty_pages_supported = false;
    bcontainer->max_dirty_bitmap_size = 0;
    bcontainer->dirty_pgsizes = 0;
    return true;
}

/*
 * DMA map/unmap are no-ops: Apple passthrough handles DMA mapping through
 * the companion apple-dma-pci device which talks to the dext directly,
 * bypassing the IOMMU container's DMA path.  The stubs are required because
 * the VFIO listener asserts they are non-NULL.
 */
static int apple_vfio_dma_map(const VFIOContainer *bcontainer, hwaddr iova,
                              uint64_t size, void *vaddr, bool readonly,
                              MemoryRegion *mr)
{
    return 0;
}

static int apple_vfio_dma_unmap(const VFIOContainer *bcontainer, hwaddr iova,
                                uint64_t size, IOMMUTLBEntry *iotlb,
                                bool unmap_all)
{
    return 0;
}

static int apple_vfio_set_dirty_page_tracking(const VFIOContainer *bcontainer,
                                              bool start, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "vfio-apple does not support migration");
    return -ENOTSUP;
}

static int apple_vfio_query_dirty_bitmap(const VFIOContainer *bcontainer,
                                         VFIOBitmap *vbmap, hwaddr iova,
                                         hwaddr size, uint64_t backend_flag,
                                         Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "vfio-apple does not support migration");
    return -ENOTSUP;
}

static int apple_vfio_pci_hot_reset(VFIODevice *vbasedev, bool single)
{
    return 0;
}

static AppleVFIOContainer *apple_vfio_container_connect(AddressSpace *as,
                                                        VFIODevice *vbasedev,
                                                        Error **errp)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(vbasedev->dev);
    AppleVFIOContainer *container;
    VFIOContainer *bcontainer;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc;
    int ret;

    space = vfio_address_space_get(as);
    container = VFIO_IOMMU_APPLE(object_new(TYPE_VFIO_IOMMU_APPLE));
    bcontainer = VFIO_IOMMU(container);
    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    container->host_bus = vdev->host.bus;
    container->host_device = vdev->host.slot;
    container->host_function = vdev->host.function;

    ret = ram_block_uncoordinated_discard_disable(true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto fail_unref;
    }

    container->dext_conn = apple_dext_connect(container->host_bus,
                                                  container->host_device,
                                                  container->host_function);
    if (container->dext_conn == IO_OBJECT_NULL) {
        error_setg(errp,
                   "vfio-apple: could not connect to dext for host PCI "
                   "%02x:%02x.%x",
                   container->host_bus, container->host_device,
                   container->host_function);
        goto fail_discards;
    }

    if (apple_dext_claim(container->dext_conn) != KERN_SUCCESS) {
        error_setg(errp,
                   "vfio-apple: failed to claim dext-backed PCI device "
                   "%02x:%02x.%x",
                   container->host_bus, container->host_device,
                   container->host_function);
        goto fail_release_conn;
    }

    if (!apple_vfio_dext_publish(container->host_bus, container->host_device,
                                 container->host_function,
                                 container->dext_conn)) {
        error_setg(errp,
                   "vfio-apple: duplicate dext owner for host PCI %02x:%02x.%x",
                   container->host_bus, container->host_device,
                   container->host_function);
        goto fail_release_conn;
    }

    if (!vioc->setup(bcontainer, errp)) {
        goto fail_shared_conn;
    }

    vfio_address_space_insert(space, bcontainer);

    if (!vfio_listener_register(bcontainer, errp)) {
        goto fail_address_space;
    }

    bcontainer->initialized = true;
    return container;

fail_address_space:
    vfio_listener_unregister(bcontainer);
    QLIST_REMOVE(bcontainer, next);
    bcontainer->space = NULL;
fail_shared_conn:
    apple_vfio_dext_release(container->host_bus, container->host_device,
                            container->host_function, container->dext_conn);
    container->dext_conn = IO_OBJECT_NULL;
fail_discards:
    ram_block_uncoordinated_discard_disable(false);
fail_unref:
    object_unref(container);
    vfio_address_space_put(space);
    return NULL;

fail_release_conn:
    apple_dext_disconnect(container->dext_conn);
    container->dext_conn = IO_OBJECT_NULL;
    goto fail_discards;
}

static void apple_vfio_container_disconnect(AppleVFIOContainer *container)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(container);
    VFIOAddressSpace *space = bcontainer->space;

    ram_block_uncoordinated_discard_disable(false);
    vfio_listener_unregister(bcontainer);

    apple_vfio_dext_release(container->host_bus, container->host_device,
                            container->host_function, container->dext_conn);
    container->dext_conn = IO_OBJECT_NULL;

    object_unref(container);
    vfio_address_space_put(space);
}

static bool apple_vfio_attach_device(const char *name, VFIODevice *vbasedev,
                                     AddressSpace *as, Error **errp)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    AppleVFIOContainer *container;
    struct vfio_device_info info = {
        .argsz = sizeof(info),
        .flags = VFIO_DEVICE_FLAGS_PCI | VFIO_DEVICE_FLAGS_RESET,
        .num_regions = VFIO_PCI_NUM_REGIONS,
        .num_irqs = VFIO_PCI_NUM_IRQS,
    };

    container = apple_vfio_container_connect(as, vbasedev, errp);
    if (!container) {
        return false;
    }

    vbasedev->fd = -1;
    vbasedev->io_ops = &apple_vfio_device_io_ops;
    vfio_device_prepare(vbasedev, VFIO_IOMMU(container), &info);

    if (!apple_vfio_device_setup(adev, errp)) {
        vfio_device_unprepare(vbasedev);
        apple_vfio_container_disconnect(container);
        return false;
    }

    return true;
}

static void apple_vfio_detach_device(VFIODevice *vbasedev)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    AppleVFIOContainer *container = VFIO_IOMMU_APPLE(vbasedev->bcontainer);

    apple_vfio_device_cleanup(adev);
    vfio_device_unprepare(vbasedev);
    apple_vfio_container_disconnect(container);
}

static void vfio_iommu_apple_class_init(ObjectClass *klass, const void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

    vioc->setup = apple_vfio_setup;
    vioc->dma_map = apple_vfio_dma_map;
    vioc->dma_unmap = apple_vfio_dma_unmap;
    vioc->attach_device = apple_vfio_attach_device;
    vioc->detach_device = apple_vfio_detach_device;
    vioc->set_dirty_page_tracking = apple_vfio_set_dirty_page_tracking;
    vioc->query_dirty_bitmap = apple_vfio_query_dirty_bitmap;
    vioc->pci_hot_reset = apple_vfio_pci_hot_reset;
}

static const TypeInfo apple_vfio_types[] = {
    {
        .name = TYPE_VFIO_IOMMU_APPLE,
        .parent = TYPE_VFIO_IOMMU,
        .instance_size = sizeof(AppleVFIOContainer),
        .class_init = vfio_iommu_apple_class_init,
    },
};

DEFINE_TYPES(apple_vfio_types)
