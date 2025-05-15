/*
 * vfio PCI device over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"

#include "hw/qdev-properties.h"
#include "hw/vfio/pci.h"
#include "hw/vfio-user/device.h"
#include "hw/vfio-user/proxy.h"

#define TYPE_VFIO_USER_PCI "vfio-user-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VFIOUserPCIDevice, VFIO_USER_PCI)

struct VFIOUserPCIDevice {
    VFIOPCIDevice device;
    char *sock_name;
    bool send_queued;   /* all sends are queued */
};

/*
 * The server maintains the device's pending interrupts,
 * via its MSIX table and PBA, so we treat these accesses
 * like PCI config space and forward them.
 */
static uint64_t vfio_user_pba_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    VFIOPCIDevice *vdev = opaque;
    VFIORegion *region = &vdev->bars[vdev->msix->pba_bar].region;
    uint64_t data;

    /* server copy is what matters */
    data = vfio_region_read(region, addr + vdev->msix->pba_offset, size);
    return data;
}

static void vfio_user_pba_write(void *opaque, hwaddr addr,
                                  uint64_t data, unsigned size)
{
    /* dropped */
}

static const MemoryRegionOps vfio_user_pba_ops = {
    .read = vfio_user_pba_read,
    .write = vfio_user_pba_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vfio_user_msix_setup(VFIOPCIDevice *vdev)
{
    MemoryRegion *vfio_reg, *msix_reg, *pba_reg;

    pba_reg = g_new0(MemoryRegion, 1);
    vdev->msix->pba_region = pba_reg;

    vfio_reg = vdev->bars[vdev->msix->pba_bar].mr;
    msix_reg = &vdev->pdev.msix_pba_mmio;
    memory_region_init_io(pba_reg, OBJECT(vdev), &vfio_user_pba_ops, vdev,
                          "VFIO MSIX PBA", int128_get64(msix_reg->size));
    memory_region_add_subregion_overlap(vfio_reg, vdev->msix->pba_offset,
                                        pba_reg, 1);
}

static void vfio_user_msix_teardown(VFIOPCIDevice *vdev)
{
    MemoryRegion *mr, *sub;

    mr = vdev->bars[vdev->msix->pba_bar].mr;
    sub = vdev->msix->pba_region;
    memory_region_del_subregion(mr, sub);

    g_free(vdev->msix->pba_region);
    vdev->msix->pba_region = NULL;
}

/*
 * Incoming request message callback.
 *
 * Runs off main loop, so BQL held.
 */
static void vfio_user_pci_process_req(void *opaque, VFIOUserMsg *msg)
{

}

/*
 * Emulated devices don't use host hot reset
 */
static void vfio_user_compute_needs_reset(VFIODevice *vbasedev)
{
    vbasedev->needs_reset = false;
}

static VFIODeviceOps vfio_user_pci_ops = {
    .vfio_compute_needs_reset = vfio_user_compute_needs_reset,
    .vfio_eoi = vfio_intx_eoi,
    .vfio_get_object = vfio_pci_get_object,
    .vfio_save_config = vfio_pci_save_config,
    .vfio_load_config = vfio_pci_load_config,
};

static void vfio_user_pci_realize(PCIDevice *pdev, Error **errp)
{
    ERRP_GUARD();
    VFIOUserPCIDevice *udev = VFIO_USER_PCI(pdev);
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    AddressSpace *as;
    SocketAddress addr;
    VFIOUserProxy *proxy;

    /*
     * TODO: make option parser understand SocketAddress
     * and use that instead of having scalar options
     * for each socket type.
     */
    if (!udev->sock_name) {
        error_setg(errp, "No socket specified");
        error_append_hint(errp, "Use -device vfio-user-pci,socket=<name>\n");
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.type = SOCKET_ADDRESS_TYPE_UNIX;
    addr.u.q_unix.path = udev->sock_name;
    proxy = vfio_user_connect_dev(&addr, errp);
    if (!proxy) {
        return;
    }
    vbasedev->proxy = proxy;
    vfio_user_set_handler(vbasedev, vfio_user_pci_process_req, vdev);

    if (udev->send_queued) {
        proxy->flags |= VFIO_PROXY_FORCE_QUEUED;
    }

    if (!vfio_user_validate_version(proxy, errp)) {
        goto error;
    }

    vbasedev->name = g_strdup_printf("VFIO user <%s>", udev->sock_name);

    /*
     * Use socket-based device I/O instead of vfio kernel driver.
     */
    vbasedev->io_ops = &vfio_user_device_io_ops_sock;

    /*
     * vfio-user devices are effectively mdevs (don't use a host iommu).
     */
    vbasedev->mdev = true;

    /*
     * Enable per-region fds.
     */
    vbasedev->use_region_fds = true;

    as = pci_device_iommu_address_space(pdev);
    if (!vfio_device_attach_by_iommu_type(TYPE_VFIO_IOMMU_USER,
                                          vbasedev->name, vbasedev,
                                          as, errp)) {
        goto error;
    }

    if (!vfio_populate_device(vdev, errp)) {
        goto error;
    }

    if (!vfio_pci_config_setup(vdev, errp)) {
        goto error;
    }

    /*
     * vfio_pci_config_setup will have registered the device's BARs
     * and setup any MSIX BARs, so errors after it succeeds must
     * use out_teardown
     */

    if (!vfio_add_capabilities(vdev, errp)) {
        goto out_teardown;
    }

    if (vdev->msix != NULL) {
        vfio_user_msix_setup(vdev);
    }

    if (!vfio_interrupt_setup(vdev, errp)) {
        goto out_teardown;
    }

    vfio_register_err_notifier(vdev);
    vfio_register_req_notifier(vdev);

    return;

out_teardown:
    vfio_teardown_msi(vdev);
    vfio_bars_exit(vdev);
error:
    error_prepend(errp, VFIO_MSG_PREFIX, vdev->vbasedev.name);
}

static void vfio_user_instance_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    device_add_bootindex_property(obj, &vdev->bootindex,
                                  "bootindex", NULL,
                                  &pci_dev->qdev);
    vdev->host.domain = ~0U;
    vdev->host.bus = ~0U;
    vdev->host.slot = ~0U;
    vdev->host.function = ~0U;

    vfio_device_init(vbasedev, VFIO_DEVICE_TYPE_PCI, &vfio_user_pci_ops,
                     DEVICE(vdev), false);

    vdev->nv_gpudirect_clique = 0xFF;

    /*
     * QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices.
     */
    pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
}

static void vfio_user_instance_finalize(Object *obj)
{
    VFIOPCIDevice *vdev = VFIO_PCI_BASE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    if (vdev->msix != NULL) {
        vfio_user_msix_teardown(vdev);
    }

    vfio_pci_put_device(vdev);

    if (vbasedev->proxy != NULL) {
        vfio_user_disconnect(vbasedev->proxy);
    }
}

static const Property vfio_user_pci_dev_properties[] = {
    DEFINE_PROP_UINT32("x-pci-vendor-id", VFIOPCIDevice,
                       vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-device-id", VFIOPCIDevice,
                       device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-vendor-id", VFIOPCIDevice,
                       sub_vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-device-id", VFIOPCIDevice,
                       sub_device_id, PCI_ANY_ID),
    DEFINE_PROP_STRING("socket", VFIOUserPCIDevice, sock_name),
    DEFINE_PROP_BOOL("x-send-queued", VFIOUserPCIDevice, send_queued, false),
};

static void vfio_user_pci_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_user_pci_dev_properties);
    dc->desc = "VFIO over socket PCI device assignment";
    pdc->realize = vfio_user_pci_realize;
}

static const TypeInfo vfio_user_pci_dev_info = {
    .name = TYPE_VFIO_USER_PCI,
    .parent = TYPE_VFIO_PCI_BASE,
    .instance_size = sizeof(VFIOUserPCIDevice),
    .class_init = vfio_user_pci_dev_class_init,
    .instance_init = vfio_user_instance_init,
    .instance_finalize = vfio_user_instance_finalize,
};

static void register_vfio_user_dev_type(void)
{
    type_register_static(&vfio_user_pci_dev_info);
}

 type_init(register_vfio_user_dev_type)
