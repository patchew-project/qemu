/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Apple/macOS VFIO PCI device passthrough via DriverKit dext.
 *
 * Copyright (c) 2026 Scott J. Goldman
 */

#include "qemu/osdep.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <linux/vfio.h>

#include "apple-dext-client.h"
#include "hw/vfio/apple.h"
#include "hw/vfio/vfio-container.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/host-pci-mmio.h"
#include "qemu/main-loop.h"

typedef struct AppleVFIOSharedDext {
    io_connect_t conn;
    uint32_t refs;
} AppleVFIOSharedDext;

typedef struct AppleVFIODMAProbe {
    uint64_t managed_bdf;
    uint64_t host_bus;
    uint64_t host_device;
    uint64_t host_function;
    DeviceState *match;
} AppleVFIODMAProbe;

static GHashTable *apple_vfio_shared_dexts;

static inline guint apple_vfio_dext_key(uint8_t bus, uint8_t device,
                                        uint8_t function)
{
    return ((guint)bus << 16) | ((guint)device << 8) | function;
}

static inline AppleVFIOContainer *apple_vfio_container(VFIODevice *vbasedev)
{
    return VFIO_IOMMU_APPLE(vbasedev->bcontainer);
}

static inline io_connect_t apple_vfio_connection(VFIODevice *vbasedev)
{
    AppleVFIOContainer *container = apple_vfio_container(vbasedev);

    return container ? container->dext_conn : IO_OBJECT_NULL;
}

static void apple_vfio_find_dma_companion_cb(PCIBus *bus, PCIDevice *pdev,
                                             void *opaque)
{
    AppleVFIODMAProbe *probe = opaque;
    Error *err = NULL;
    uint64_t managed_bdf;
    uint64_t host_bus;
    uint64_t host_device;
    uint64_t host_function;

    if (probe->match ||
        !object_dynamic_cast(OBJECT(pdev), "apple-dma-pci")) {
        return;
    }

    managed_bdf = object_property_get_uint(OBJECT(pdev), "managed-bdf", &err);
    if (err) {
        error_free(err);
        return;
    }

    host_bus = object_property_get_uint(OBJECT(pdev), "x-apple-host-bus", &err);
    if (err) {
        error_free(err);
        return;
    }

    host_device = object_property_get_uint(OBJECT(pdev),
                                           "x-apple-host-device", &err);
    if (err) {
        error_free(err);
        return;
    }

    host_function = object_property_get_uint(OBJECT(pdev),
                                             "x-apple-host-function", &err);
    if (err) {
        error_free(err);
        return;
    }

    if (managed_bdf == probe->managed_bdf &&
        host_bus == probe->host_bus &&
        host_device == probe->host_device &&
        host_function == probe->host_function) {
        probe->match = DEVICE(pdev);
    }
}

static DeviceState *apple_vfio_find_dma_companion(VFIOApplePCIDevice *adev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);
    PCIDevice *pdev = PCI_DEVICE(vdev);
    AppleVFIODMAProbe probe = {
        .managed_bdf = PCI_BUILD_BDF(pci_dev_bus_num(pdev), pdev->devfn),
        .host_bus = vdev->host.bus,
        .host_device = vdev->host.slot,
        .host_function = vdev->host.function,
    };

    pci_for_each_device_under_bus(pci_device_root_bus(pdev),
                                  apple_vfio_find_dma_companion_cb, &probe);
    return probe.match;
}

static void apple_vfio_signal_irqfd(int fd)
{
    static const uint64_t value = 1;
    ssize_t ret;

    if (fd < 0) {
        return;
    }

    do {
        ret = write(fd, &value, sizeof(value));
    } while (ret < 0 && errno == EINTR);
}

static void apple_vfio_deliver_irq(VFIOPCIDevice *vdev, uint32_t vector)
{
    switch (vdev->interrupt) {
    case VFIO_INT_MSI:
    case VFIO_INT_MSIX:
        if (vector < vdev->nr_vectors && vdev->msi_vectors[vector].use) {
            apple_vfio_signal_irqfd(
                event_notifier_get_wfd(&vdev->msi_vectors[vector].interrupt));
        }
        break;
    case VFIO_INT_INTx:
        apple_vfio_signal_irqfd(
            event_notifier_get_wfd(&vdev->intx.interrupt));
        break;
    default:
        break;
    }
}

/*
 * Called on a GCD dispatch queue when the dext signals pending interrupts.
 * Just pokes the EventNotifier to wake the QEMU main loop.
 */
static void apple_vfio_irq_wakeup(void *opaque)
{
    VFIOApplePCIDevice *adev = opaque;

    event_notifier_set(&adev->apple->irq_notifier);
}

/*
 * QEMU main-loop fd handler: drain the pending-interrupt bitfield from
 * the dext, deliver each flagged vector, then re-arm the async wait.
 */
static void apple_vfio_irq_handler(void *opaque)
{
    VFIOApplePCIDevice *adev = opaque;
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    io_connect_t conn = apple_vfio_connection(vbasedev);
    AppleVFIOState *apple = adev->apple;
    uint64_t pending[4];
    int word;

    if (!event_notifier_test_and_clear(&apple->irq_notifier)) {
        return;
    }

    if (conn == IO_OBJECT_NULL) {
        return;
    }

    if (apple_dext_read_pending_irqs(conn, pending) != KERN_SUCCESS) {
        apple_dext_interrupt_notify_rearm(apple->irq_notify);
        return;
    }

    for (word = 0; word < 4; word++) {
        uint64_t bits = pending[word];

        while (bits) {
            int bit = __builtin_ctzll(bits);
            uint32_t vector = word * 64 + bit;

            apple_vfio_deliver_irq(vdev, vector);
            bits &= bits - 1;
        }
    }

    apple_dext_interrupt_notify_rearm(apple->irq_notify);
}

bool apple_vfio_get_bar_info(VFIOApplePCIDevice *adev, uint8_t bar,
                             uint8_t *mem_idx, uint64_t *size,
                             uint8_t *type)
{
    io_connect_t conn = apple_vfio_connection(&VFIO_PCI_DEVICE(adev)->vbasedev);

    if (conn != IO_OBJECT_NULL) {
        return apple_dext_get_bar_info(conn, bar, mem_idx, size, type) ==
               KERN_SUCCESS;
    }

    if (mem_idx) {
        *mem_idx = 0;
    }
    if (size) {
        *size = 0;
    }
    if (type) {
        *type = 0;
    }
    return false;
}

static void apple_vfio_pci_init(VFIOApplePCIDevice *adev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);

    /*
     * On macOS, HVF can only map on 16kb page boundaries, so these quirk
     * fixes end up breaking things. Likewise the performance enhancements
     * there rely on kvm-specific features. Disable for now, but we should
     * revisit this.
     */
    vdev->no_bar_quirks = true;
}

static bool apple_vfio_pci_pre_realize(VFIOApplePCIDevice *adev, Error **errp)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);
    VFIODevice *vbasedev = &vdev->vbasedev;

    adev->apple = g_new0(AppleVFIOState, 1);

    if (!vbasedev->name) {
        vbasedev->name = g_strdup_printf("apple-%04x:%02x:%02x.%x",
                                         vdev->host.domain,
                                         vdev->host.bus,
                                         vdev->host.slot,
                                         vdev->host.function);
    }

    return true;
}

static bool apple_vfio_create_dma_companion(VFIOApplePCIDevice *adev,
                                            Error **errp)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);
    PCIDevice *pdev = PCI_DEVICE(vdev);
    DeviceState *dev;

    if (adev->dma_companion_autocreated && adev->dma_companion) {
        return true;
    }

    if (apple_vfio_find_dma_companion(adev) != NULL) {
        return true;
    }

    dev = qdev_new("apple-dma-pci");
    if (!object_property_set_uint(OBJECT(dev), "managed-bdf",
                                  PCI_BUILD_BDF(pci_dev_bus_num(pdev),
                                                pdev->devfn), errp) ||
        !object_property_set_uint(OBJECT(dev), "x-apple-host-bus",
                                  vdev->host.bus, errp) ||
        !object_property_set_uint(OBJECT(dev), "x-apple-host-device",
                                  vdev->host.slot, errp) ||
        !object_property_set_uint(OBJECT(dev), "x-apple-host-function",
                                  vdev->host.function, errp)) {
        object_unref(OBJECT(dev));
        return false;
    }

    if (!qdev_realize(dev, BUS(pci_get_bus(pdev)), errp)) {
        object_unref(OBJECT(dev));
        return false;
    }

    adev->dma_companion = dev;
    adev->dma_companion_autocreated = true;
    object_unref(OBJECT(dev));
    return true;
}

static void apple_vfio_destroy_dma_companion(VFIOApplePCIDevice *adev)
{
    if (!adev->dma_companion_autocreated || adev->dma_companion == NULL) {
        return;
    }

    object_unparent(OBJECT(adev->dma_companion));
    adev->dma_companion = NULL;
    adev->dma_companion_autocreated = false;
}

bool apple_vfio_device_setup(VFIOApplePCIDevice *adev, Error **errp)
{
    VFIODevice *vbasedev = &VFIO_PCI_DEVICE(adev)->vbasedev;
    io_connect_t conn = apple_vfio_connection(vbasedev);
    uint32_t num_vectors = 0;
    kern_return_t kr;

    if (conn == IO_OBJECT_NULL) {
        error_setg(errp, "vfio-apple: missing dext connection");
        return false;
    }

    kr = apple_dext_setup_interrupts(conn, &num_vectors);
    if (kr != KERN_SUCCESS) {
        error_setg(errp, "vfio-apple: failed to setup interrupts (kr=0x%x)",
                   kr);
        return false;
    }

    adev->apple->num_irq_vectors = num_vectors;

    if (event_notifier_init(&adev->apple->irq_notifier, false) < 0) {
        error_setg(errp, "vfio-apple: failed to create IRQ event notifier");
        return false;
    }

    qemu_set_fd_handler(event_notifier_get_fd(&adev->apple->irq_notifier),
                        apple_vfio_irq_handler, NULL, adev);

    adev->apple->irq_notify =
        apple_dext_interrupt_notify_create(conn, apple_vfio_irq_wakeup, adev);
    if (!adev->apple->irq_notify) {
        error_setg(errp,
                   "vfio-apple: failed to create IRQ async notification");
        qemu_set_fd_handler(
            event_notifier_get_fd(&adev->apple->irq_notifier),
            NULL, NULL, NULL);
        event_notifier_cleanup(&adev->apple->irq_notifier);
        return false;
    }

    return true;
}

void apple_vfio_device_cleanup(VFIOApplePCIDevice *adev)
{
    AppleVFIOState *apple = adev->apple;

    if (!apple) {
        return;
    }

    if (apple->irq_notify) {
        apple_dext_interrupt_notify_destroy(apple->irq_notify);
        apple->irq_notify = NULL;

        qemu_set_fd_handler(event_notifier_get_fd(&apple->irq_notifier),
                            NULL, NULL, NULL);
        event_notifier_cleanup(&apple->irq_notifier);
    }
}

static int apple_vfio_device_feature(VFIODevice *vdev,
                                     struct vfio_device_feature *feat)
{
    return -ENOTTY;
}

static int apple_vfio_device_reset(VFIODevice *vbasedev)
{
    io_connect_t conn = apple_vfio_connection(vbasedev);

    if (conn == IO_OBJECT_NULL) {
        return -ENODEV;
    }

    return apple_dext_reset_device(conn) == KERN_SUCCESS ? 0 : -EIO;
}

static int apple_vfio_get_region_info(VFIODevice *vbasedev,
                                      struct vfio_region_info *info,
                                      int *fd)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    uint32_t index = info->index;
    uint64_t size = 0;

    if (fd) {
        *fd = -1;
    }

    memset((char *)info + offsetof(struct vfio_region_info, flags), 0,
           sizeof(*info) - offsetof(struct vfio_region_info, flags));

    info->index = index;
    info->flags = VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE;
    info->offset = (uint64_t)index << 20;

    switch (info->index) {
    case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:
        if (!apple_vfio_get_bar_info(adev, info->index, NULL, &size, NULL)) {
            size = 0;
        }
        info->size = size;
        info->flags |= VFIO_REGION_INFO_FLAG_MMAP;
        break;
    case VFIO_PCI_CONFIG_REGION_INDEX:
        info->size = PCIE_CONFIG_SPACE_SIZE;
        break;
    case VFIO_PCI_ROM_REGION_INDEX:
    case VFIO_PCI_VGA_REGION_INDEX:
        info->size = 0;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int apple_vfio_get_irq_info(VFIODevice *vbasedev,
                                   struct vfio_irq_info *info)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);

    switch (info->index) {
    case VFIO_PCI_MSI_IRQ_INDEX:
        info->flags = VFIO_IRQ_INFO_EVENTFD;
        info->count = adev->apple->num_irq_vectors;
        break;
    case VFIO_PCI_MSIX_IRQ_INDEX:
        info->flags = VFIO_IRQ_INFO_EVENTFD | VFIO_IRQ_INFO_NORESIZE;
        info->count = vdev->msix ? vdev->msix->entries : 0;
        break;
    case VFIO_PCI_INTX_IRQ_INDEX:
        info->flags = VFIO_IRQ_INFO_EVENTFD;
        info->count = 1;
        break;
    case VFIO_PCI_ERR_IRQ_INDEX:
    case VFIO_PCI_REQ_IRQ_INDEX:
        /*
         * Apple dext passthrough has no kernel-side AER or device-request
         * notification currently; return count 0 to tell the core to skip
         * these.
         */
        info->flags = 0;
        info->count = 0;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static void apple_vfio_update_irq_mask(VFIODevice *vbasedev)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);
    io_connect_t conn = apple_vfio_connection(vbasedev);
    uint64_t mask[4] = {0};
    uint32_t i;

    if (conn == IO_OBJECT_NULL) {
        return;
    }

    switch (vdev->interrupt) {
    case VFIO_INT_MSI:
    case VFIO_INT_MSIX:
        for (i = 0; i < vdev->nr_vectors; i++) {
            if (vdev->msi_vectors[i].use) {
                mask[i / 64] |= 1ULL << (i % 64);
            }
        }
        break;
    case VFIO_INT_INTx:
        mask[0] = 1;
        break;
    default:
        break;
    }

    apple_dext_set_irq_mask(conn, mask);
}

static int apple_vfio_set_irqs(VFIODevice *vbasedev, struct vfio_irq_set *irq)
{
    apple_vfio_update_irq_mask(vbasedev);
    return 0;
}

static int apple_vfio_bar_read(VFIODevice *vbasedev, uint8_t nr, off_t off,
                               uint32_t size, void *data)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    AppleVFIOBarMap *bm = &adev->apple->bar_maps[nr];
    const void *p;
    uint64_t value;

    if (!bm->addr || off + size > bm->size) {
        error_report("vfio-apple: BAR%d read out of range or unmapped", nr);
        return -EINVAL;
    }

    if (size != 1 && size != 2 && size != 4 && size != 8) {
        return -EINVAL;
    }

    p = (const char *)bm->addr + off;
    value = host_pci_ldn_le_p(p, size);
    memcpy(data, &value, size);

    return size;
}

static int apple_vfio_region_read(VFIODevice *vbasedev, uint8_t nr, off_t off,
                                  uint32_t size, void *data)
{
    io_connect_t conn = apple_vfio_connection(vbasedev);
    kern_return_t kr;
    uint32_t legacy_size = 0;

    if (nr != VFIO_PCI_CONFIG_REGION_INDEX) {
        return apple_vfio_bar_read(vbasedev, nr, off, size, data);
    }

    if (conn == IO_OBJECT_NULL) {
        return -ENODEV;
    }

    legacy_size = MIN(size, PCIE_CONFIG_SPACE_SIZE - off);

    if (legacy_size == 1 || legacy_size == 2 || legacy_size == 4) {
        uint64_t value = 0;

        kr = apple_dext_config_read(conn, off, legacy_size, &value);
        if (kr != KERN_SUCCESS) {
            return -EIO;
        }

        memcpy(data, &value, legacy_size);
        if (legacy_size < size) {
            memset((uint8_t *)data + legacy_size, 0, size - legacy_size);
        }
        return size;
    }

    kr = apple_dext_config_read_block(conn, off, data, legacy_size);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }
    if (legacy_size < size) {
        memset((uint8_t *)data + legacy_size, 0, size - legacy_size);
    }
    return size;
}

static bool apple_vfio_config_write_is_safe(off_t off, uint32_t size)
{
    off_t end = off + size;

    /*
     * Block writes that would reprogram the device's bus identity or
     * address decoders.  macOS / DART owns those registers; touching
     * them from the guest breaks the IOKit mapping and the device
     * "falls off the bus."
     *
     * Everything else (vendor capabilities, MSI/MSI-X, PCIe cap, etc.)
     * is forwarded.
     */

    /* PCI_STATUS stays emulated/blocked */
    if (off < PCI_STATUS + 2 && end > PCI_STATUS) {
        return false;
    }

    /* BAR0-BAR5 */
    if (off < PCI_BASE_ADDRESS_5 + 4 && end > PCI_BASE_ADDRESS_0) {
        return false;
    }

    return true;
}

static int apple_vfio_forward_command_write(io_connect_t conn, off_t off,
                                            uint32_t size, const void *data)
{
    const uint8_t *bytes = data;
    off_t end = off + size;
    off_t cmd_start = MAX(off, (off_t)PCI_COMMAND);
    off_t cmd_end = MIN(end, (off_t)(PCI_COMMAND + 2));
    off_t pos;

    if (conn == IO_OBJECT_NULL) {
        return -ENODEV;
    }

    for (pos = cmd_start; pos < cmd_end; pos++) {
        uint64_t value = bytes[pos - off];
        kern_return_t kr = apple_dext_config_write(conn, pos, 1, value);

        if (kr != KERN_SUCCESS) {
            return -EIO;
        }
    }

    return 0;
}

static int apple_vfio_bar_write(VFIODevice *vbasedev, uint8_t nr, off_t off,
                                uint32_t size, void *data)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    AppleVFIOBarMap *bm = &adev->apple->bar_maps[nr];
    void *p;
    uint64_t value = 0;

    if (!bm->addr || off + size > bm->size) {
        error_report("vfio-apple: BAR%d write out of range or unmapped", nr);
        return -EINVAL;
    }

    if (size != 1 && size != 2 && size != 4 && size != 8) {
        return -EINVAL;
    }

    p = (char *)bm->addr + off;
    memcpy(&value, data, size);
    host_pci_stn_le_p(p, size, value);

    return size;
}

static int apple_vfio_region_write(VFIODevice *vbasedev, uint8_t nr, off_t off,
                                   uint32_t size, void *data, bool post)
{
    io_connect_t conn = apple_vfio_connection(vbasedev);
    uint64_t value = 0;
    kern_return_t kr;
    uint32_t legacy_size;

    if (nr != VFIO_PCI_CONFIG_REGION_INDEX) {
        return apple_vfio_bar_write(vbasedev, nr, off, size, data);
    }

    if (off < PCI_COMMAND + 2 && off + size > PCI_COMMAND) {
        int ret = apple_vfio_forward_command_write(conn, off, size, data);

        if (ret) {
            return ret;
        }

        if (off >= PCI_COMMAND && off + size <= PCI_COMMAND + 2) {
            return size;
        }
    }

    if (!apple_vfio_config_write_is_safe(off, size)) {
        return size;
    }

    if (conn == IO_OBJECT_NULL) {
        return -ENODEV;
    }

    memcpy(&value, data, size);
    legacy_size = MIN(size, PCIE_CONFIG_SPACE_SIZE - off);
    if (!(legacy_size == 1 || legacy_size == 2 || legacy_size == 4)) {
        return -EINVAL;
    }

    kr = apple_dext_config_write(conn, off, legacy_size, value);
    if (kr != KERN_SUCCESS) {
        return -EIO;
    }
    return size;
}

static int apple_vfio_region_map(VFIODevice *vbasedev, VFIORegion *region);
static void apple_vfio_region_unmap(VFIODevice *vbasedev, VFIORegion *region);

static int apple_vfio_region_map(VFIODevice *vbasedev, VFIORegion *region)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(adev);
    io_connect_t conn = apple_vfio_connection(vbasedev);
    int bar = region->nr;
    VFIOBAR *vbar;
    mach_vm_address_t local_addr = 0;
    mach_vm_size_t bar_size = 0;
    uint8_t bar_type = 0;
    kern_return_t kr;
    int i;

    if (bar < VFIO_PCI_BAR0_REGION_INDEX || bar >= VFIO_PCI_ROM_REGION_INDEX) {
        return 0;
    }

    vbar = &vdev->bars[bar];

    if (conn == IO_OBJECT_NULL || !vbar->size || vbar->ioport) {
        return 0;
    }

    if (bar > 0 && vdev->bars[bar - 1].mem64) {
        return 0;
    }

    if (adev->apple->bar_maps[bar].addr != NULL) {
        return 0;
    }

    kr = apple_dext_map_bar(conn, bar, &local_addr, &bar_size, &bar_type);
    if (kr != KERN_SUCCESS) {
        warn_report("vfio-apple: BAR%d map failed for %s: 0x%x",
                    bar, vbasedev->name, kr);
        return -EIO;
    }

    if (bar_size > vbar->size) {
        bar_size = vbar->size;
    }

    adev->apple->bar_maps[bar].addr = (void *)local_addr;
    adev->apple->bar_maps[bar].size = bar_size;

    /*
     * Use the pre-computed mmap regions — already split around the MSI-X
     * table/PBA hole by vfio_pci_fixup_msix_region() during realize.
     * We just need to fill in the host pointers from our dext mapping.
     */
    for (i = 0; i < region->nr_mmaps; i++) {
        region->mmaps[i].mmap = (char *)local_addr + region->mmaps[i].offset;
        vfio_region_register_mmap(region, i);
    }

    return 0;
}

static void apple_vfio_region_unmap(VFIODevice *vbasedev, VFIORegion *region)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(vbasedev->dev);
    io_connect_t conn = apple_vfio_connection(vbasedev);
    int bar = region->nr;
    AppleVFIOBarMap *bm;
    int i;

    if (bar < VFIO_PCI_BAR0_REGION_INDEX || bar >= VFIO_PCI_ROM_REGION_INDEX) {
        return;
    }

    bm = &adev->apple->bar_maps[bar];

    for (i = 0; i < region->nr_mmaps; i++) {
        if (region->mmaps[i].mmap) {
            vfio_region_unregister_mmap(region, i);
            region->mmaps[i].mmap = NULL;
        }
    }

    if (conn != IO_OBJECT_NULL && bm->addr != NULL) {
        apple_dext_unmap_bar(conn, bar, (mach_vm_address_t)bm->addr);
    }

    bm->addr = NULL;
    bm->size = 0;
}

VFIODeviceIOOps apple_vfio_device_io_ops = {
    .device_feature = apple_vfio_device_feature,
    .get_region_info = apple_vfio_get_region_info,
    .get_irq_info = apple_vfio_get_irq_info,
    .set_irqs = apple_vfio_set_irqs,
    .device_reset = apple_vfio_device_reset,
    .region_read = apple_vfio_region_read,
    .region_write = apple_vfio_region_write,
    .region_map = apple_vfio_region_map,
    .region_unmap = apple_vfio_region_unmap,
};

bool apple_vfio_dext_publish(uint8_t bus, uint8_t device, uint8_t function,
                             io_connect_t conn)
{
    AppleVFIOSharedDext *shared;
    guint key = apple_vfio_dext_key(bus, device, function);

    if (!apple_vfio_shared_dexts) {
        apple_vfio_shared_dexts =
            g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    }

    if (g_hash_table_lookup(apple_vfio_shared_dexts, GUINT_TO_POINTER(key))) {
        return false;
    }

    shared = g_new0(AppleVFIOSharedDext, 1);
    shared->conn = conn;
    shared->refs = 1;
    g_hash_table_insert(apple_vfio_shared_dexts, GUINT_TO_POINTER(key), shared);
    return true;
}

io_connect_t apple_vfio_dext_lookup(uint8_t bus, uint8_t device,
                                    uint8_t function)
{
    AppleVFIOSharedDext *shared;
    guint key = apple_vfio_dext_key(bus, device, function);

    if (!apple_vfio_shared_dexts) {
        return IO_OBJECT_NULL;
    }

    shared = g_hash_table_lookup(apple_vfio_shared_dexts,
                                 GUINT_TO_POINTER(key));
    if (!shared) {
        return IO_OBJECT_NULL;
    }

    shared->refs++;
    return shared->conn;
}

void apple_vfio_dext_release(uint8_t bus, uint8_t device, uint8_t function,
                             io_connect_t conn)
{
    AppleVFIOSharedDext *shared;
    guint key = apple_vfio_dext_key(bus, device, function);

    if (!apple_vfio_shared_dexts) {
        return;
    }

    shared = g_hash_table_lookup(apple_vfio_shared_dexts,
                                 GUINT_TO_POINTER(key));
    if (!shared || shared->conn != conn) {
        return;
    }

    if (--shared->refs == 0) {
        apple_dext_disconnect(conn);
        g_hash_table_remove(apple_vfio_shared_dexts, GUINT_TO_POINTER(key));
    }
}

/* ------------------------------------------------------------------ */
/* QOM type: vfio-apple-pci                                           */
/* ------------------------------------------------------------------ */

static void (*parent_realize)(PCIDevice *, Error **);
static void (*parent_exit)(PCIDevice *);

static void apple_vfio_pci_instance_init(Object *obj)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(obj);

    apple_vfio_pci_init(adev);
}

static void apple_vfio_pci_realize_fn(PCIDevice *pdev, Error **errp)
{
    ERRP_GUARD();
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(pdev);

    if (!apple_vfio_pci_pre_realize(adev, errp)) {
        return;
    }

    parent_realize(pdev, errp);
    if (*errp) {
        g_clear_pointer(&adev->apple, g_free);
        return;
    }

    if (!apple_vfio_create_dma_companion(adev, errp)) {
        if (parent_exit) {
            parent_exit(pdev);
        }
        g_clear_pointer(&adev->apple, g_free);
        return;
    }
}

static void apple_vfio_pci_exit_fn(PCIDevice *pdev)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(pdev);

    apple_vfio_destroy_dma_companion(adev);

    if (parent_exit) {
        parent_exit(pdev);
    }
}

static void apple_vfio_pci_finalize_fn(Object *obj)
{
    VFIOApplePCIDevice *adev = VFIO_APPLE_PCI(obj);

    apple_vfio_device_cleanup(adev);
    g_clear_pointer(&adev->apple, g_free);
}

static void apple_vfio_pci_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    parent_realize = pdc->realize;
    parent_exit = pdc->exit;

    pdc->realize = apple_vfio_pci_realize_fn;
    pdc->exit = apple_vfio_pci_exit_fn;
    dc->user_creatable = true;
    dc->desc = "VFIO-based PCI device assignment (Apple/macOS)";
}

static const TypeInfo vfio_apple_pci_info = {
    .name = TYPE_VFIO_APPLE_PCI,
    .parent = TYPE_VFIO_PCI,
    .instance_size = sizeof(VFIOApplePCIDevice),
    .instance_init = apple_vfio_pci_instance_init,
    .instance_finalize = apple_vfio_pci_finalize_fn,
    .class_init = apple_vfio_pci_class_init,
};

static void register_vfio_apple_pci_type(void)
{
    type_register_static(&vfio_apple_pci_info);
}

type_init(register_vfio_apple_pci_type)
