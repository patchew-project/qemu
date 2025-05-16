/*
 * Container for vfio-user IOMMU type: rather than communicating with the kernel
 * vfio driver, we communicate over a socket to a server using the vfio-user
 * protocol.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qemu/osdep.h"

#include "hw/vfio-user/container.h"
#include "hw/vfio-user/device.h"
#include "hw/vfio/vfio-cpr.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/vfio-listener.h"
#include "qapi/error.h"
#include "trace.h"

static int vfio_user_dma_unmap(const VFIOContainerBase *bcontainer,
                               hwaddr iova, ram_addr_t size,
                               IOMMUTLBEntry *iotlb, bool unmap_all)
{
    return -ENOTSUP;
}

static int vfio_user_dma_map(const VFIOContainerBase *bcontainer, hwaddr iova,
                             ram_addr_t size, void *vaddr, bool readonly,
                             MemoryRegion *mrp)
{
    return -ENOTSUP;
}

static int
vfio_user_set_dirty_page_tracking(const VFIOContainerBase *bcontainer,
                                    bool start, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static int vfio_user_query_dirty_bitmap(const VFIOContainerBase *bcontainer,
                                         VFIOBitmap *vbmap, hwaddr iova,
                                         hwaddr size, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static bool vfio_user_setup(VFIOContainerBase *bcontainer, Error **errp)
{
    VFIOUserContainer *container = container_of(bcontainer, VFIOUserContainer,
                                                bcontainer);

    assert(container->proxy->dma_pgsizes != 0);
    bcontainer->pgsizes = container->proxy->dma_pgsizes;
    bcontainer->dma_max_mappings = container->proxy->max_dma;

    /* No live migration support yet. */
    bcontainer->dirty_pages_supported = false;
    bcontainer->max_dirty_bitmap_size = container->proxy->max_bitmap;
    bcontainer->dirty_pgsizes = container->proxy->migr_pgsize;

    return true;
}

static VFIOUserContainer *vfio_user_create_container(VFIODevice *vbasedev,
                                                     Error **errp)
{
    VFIOUserContainer *container;

    container = VFIO_IOMMU_USER(object_new(TYPE_VFIO_IOMMU_USER));
    container->proxy = vbasedev->proxy;
    return container;
}

/*
 * Try to mirror vfio_container_connect() as much as possible.
 */
static VFIOUserContainer *
vfio_user_container_connect(AddressSpace *as, VFIODevice *vbasedev,
                            Error **errp)
{
    VFIOContainerBase *bcontainer;
    VFIOUserContainer *container;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc;
    int ret;

    space = vfio_address_space_get(as);

    container = vfio_user_create_container(vbasedev, errp);
    if (!container) {
        goto put_space_exit;
    }

    bcontainer = &container->bcontainer;

    if (!vfio_cpr_register_container(bcontainer, errp)) {
        goto free_container_exit;
    }

    ret = ram_block_uncoordinated_discard_disable(true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto unregister_container_exit;
    }

    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    assert(vioc->setup);

    if (!vioc->setup(bcontainer, errp)) {
        goto enable_discards_exit;
    }

    vfio_address_space_insert(space, bcontainer);

    if (!vfio_listener_register(bcontainer, errp)) {
        goto listener_release_exit;
    }

    bcontainer->initialized = true;

    return container;

listener_release_exit:
    vfio_listener_unregister(bcontainer);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

enable_discards_exit:
    ram_block_uncoordinated_discard_disable(false);

unregister_container_exit:
    vfio_cpr_unregister_container(bcontainer);

free_container_exit:
    object_unref(container);

put_space_exit:
    vfio_address_space_put(space);

    return NULL;
}

static void vfio_user_container_disconnect(VFIOUserContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    VFIOAddressSpace *space = bcontainer->space;

    ram_block_uncoordinated_discard_disable(false);

    vfio_listener_unregister(bcontainer);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

    vfio_cpr_unregister_container(bcontainer);
    object_unref(container);

    vfio_address_space_put(space);
}

static bool vfio_user_device_get(VFIOUserContainer *container,
                                 VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_info info = { .argsz = sizeof(info) };
    int ret;

    ret = vfio_user_get_device_info(vbasedev->proxy, &info);
    if (ret) {
        error_setg_errno(errp, -ret, "get info failure");
        return ret;
    }

    vbasedev->fd = -1;

    vfio_device_prepare(vbasedev, &container->bcontainer, &info);

    return true;
}

/*
 * vfio_user_device_attach: attach a device to a new container.
 */
static bool vfio_user_device_attach(const char *name, VFIODevice *vbasedev,
                                    AddressSpace *as, Error **errp)
{
    VFIOUserContainer *container;

    container = vfio_user_container_connect(as, vbasedev, errp);
    if (container == NULL) {
        error_prepend(errp, "failed to connect proxy");
        return false;
    }

    return vfio_user_device_get(container, vbasedev, errp);
}

static void vfio_user_device_detach(VFIODevice *vbasedev)
{
    VFIOUserContainer *container = container_of(vbasedev->bcontainer,
                                                VFIOUserContainer, bcontainer);

    vfio_device_unprepare(vbasedev);

    vfio_user_container_disconnect(container);
}

static int vfio_user_pci_hot_reset(VFIODevice *vbasedev, bool single)
{
    /* ->needs_reset is always false for vfio-user. */
    return 0;
}

static void vfio_iommu_user_class_init(ObjectClass *klass, const void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

    vioc->setup = vfio_user_setup;
    vioc->dma_map = vfio_user_dma_map;
    vioc->dma_unmap = vfio_user_dma_unmap;
    vioc->attach_device = vfio_user_device_attach;
    vioc->detach_device = vfio_user_device_detach;
    vioc->set_dirty_page_tracking = vfio_user_set_dirty_page_tracking;
    vioc->query_dirty_bitmap = vfio_user_query_dirty_bitmap;
    vioc->pci_hot_reset = vfio_user_pci_hot_reset;
};

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU_USER,
        .parent = TYPE_VFIO_IOMMU,
        .instance_size = sizeof(VFIOUserContainer),
        .class_init = vfio_iommu_user_class_init,
    },
};

DEFINE_TYPES(types)
