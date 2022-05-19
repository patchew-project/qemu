/*
 * Virtio Vhost-user Device
 *
 * Copyright (C) 2017-2018 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-vhost-user.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/pci/msix.h"

typedef struct VirtIOVhostUserPCI VirtIOVhostUserPCI;

/*
 * virtio-vhost-user-pci: This extends VirtioPCIProxy.
 */

#define TYPE_VIRTIO_VHOST_USER_PCI "virtio-vhost-user-pci-base"
#define VIRTIO_VHOST_USER_PCI(obj) OBJECT_CHECK(VirtIOVhostUserPCI, \
    (obj), TYPE_VIRTIO_VHOST_USER_PCI)
#define VIRTIO_VHOST_USER_PCI_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioVhostUserPCIClass, obj, \
        TYPE_VIRTIO_VHOST_USER_PCI)
#define VIRTIO_VHOST_USER_PCI_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioVhostUserPCIClass, klass, \
        TYPE_VIRTIO_VHOST_USER_PCI)

struct VirtIOVhostUserPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOVhostUser vdev;

    MemoryRegion additional_resources_bar;

    VirtIOPCIRegion doorbells;
    VirtIOPCIRegion notifications;
    VirtIOPCIRegion shared_memory;
};

typedef struct VirtioVhostUserPCIClass {
    VirtioPCIClass parent_class;

    void (*set_vhost_mem_regions)(VirtIOVhostUserPCI *vvup);
    void (*delete_vhost_mem_region)(VirtIOVhostUserPCI *vvup, MemoryRegion *mr);
    void (*cleanup_bar)(VirtIOVhostUserPCI *vvup);
    void (*register_doorbell)(VirtIOVhostUserPCI *vvup, EventNotifier *e,
                              uint8_t vq_idx);
    void (*unregister_doorbell)(VirtIOVhostUserPCI *vvup, EventNotifier *e,
                                uint8_t vq_idx);
} VirtioVhostUserPCIClass;

static Property virtio_vhost_user_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

/*
 * Handler for the frontend kickfd notifications. Inject an INTx or MSI-X
 * interrupt to the guest in response to the frontend notification. Use the
 * appropriate vector in the latter case.
 */
void virtio_vhost_user_guest_notifier_read(EventNotifier *n)
{
    struct kickfd *kickfd = container_of(n, struct kickfd, guest_notifier);
    VirtIODevice *vdev = kickfd->vdev;
    VirtIOVhostUser *vvu = container_of(vdev, struct VirtIOVhostUser,
        parent_obj);
    VirtIOVhostUserPCI *vvup = container_of(vvu, struct VirtIOVhostUserPCI,
        vdev);
    VirtIOPCIProxy *proxy = &vvup->parent_obj;
    PCIDevice *pci_dev = &proxy->pci_dev;

    if (event_notifier_test_and_clear(n)) {
        /*
         * The ISR status register is used only for INTx interrupts. Thus, we
         * use it only in this case.
         */
        if (!msix_enabled(pci_dev)) {
            virtio_set_isr(vdev, 0x2);
        }
        /*
         * Send an interrupt, either with INTx or MSI-X mechanism. msix_notify()
         * already handles the case where the MSI-X vector is NO_VECTOR by not
         * issuing interrupts. Thus, we don't have to check this case here.
         */
        virtio_notify_vector(vdev, kickfd->msi_vector);

        trace_virtio_vhost_user_guest_notifier_read(kickfd->guest_notifier.rfd,
            kickfd->msi_vector);
    }
}

static uint64_t virtio_vhost_user_doorbells_read(void *opaque, hwaddr addr,
                                                 unsigned size)
{
    return 0;
}

static void virtio_vhost_user_doorbells_write(void *opaque, hwaddr addr,
                                              uint64_t val, unsigned size)
{
    VirtIOVhostUserPCI *vvup = opaque;
    VirtIOPCIProxy *proxy = &vvup->parent_obj;
    VirtIOVhostUser *s = &vvup->vdev;
    unsigned idx = addr / virtio_pci_queue_mem_mult(proxy);

    if (idx < VIRTIO_QUEUE_MAX) {
        /* We shouldn't reach at this point since we are using ioeventfds. */
        if (event_notifier_get_fd(&s->callfds[idx]) >= 0) {
            ssize_t nwritten;

            nwritten = event_notifier_set(&s->callfds[idx]);
            trace_virtio_vhost_user_doorbell_write(s, idx, nwritten);

        }
    } else if (idx == VIRTIO_QUEUE_MAX) {
        /* TODO log doorbell */
   }
}

static void vvu_register_doorbell(VirtIOVhostUserPCI *vvup, EventNotifier *e,
                                  uint8_t vq_idx)
{
    VirtIOPCIProxy *proxy = &vvup->parent_obj;
    hwaddr addr = vq_idx * virtio_pci_queue_mem_mult(proxy);

    /* Register the callfd EventNotifier as ioeventfd */
    memory_region_add_eventfd(&vvup->doorbells.mr, addr, 2, false, vq_idx, e);
}

void virtio_vhost_user_register_doorbell(VirtIOVhostUser *s, EventNotifier *e,
                                         uint8_t vq_idx)
{
    VirtIOVhostUserPCI *vvup = container_of(s, struct VirtIOVhostUserPCI, vdev);
    VirtioVhostUserPCIClass *vvup_class = VIRTIO_VHOST_USER_PCI_GET_CLASS(vvup);

    vvup_class->register_doorbell(vvup, e, vq_idx);
}

static void vvu_unregister_doorbell(VirtIOVhostUserPCI *vvup, EventNotifier *e,
                                    uint8_t vq_idx)
{
    VirtIOPCIProxy *proxy = &vvup->parent_obj;
    hwaddr addr = vq_idx * virtio_pci_queue_mem_mult(proxy);

    /* Unregister the callfd EventNotifier */
    memory_region_del_eventfd(&vvup->doorbells.mr, addr, 2, false, vq_idx, e);
}

void virtio_vhost_user_unregister_doorbell(VirtIOVhostUser *s, EventNotifier *e,
                                           uint8_t vq_idx)
{
    VirtIOVhostUserPCI *vvup = container_of(s, struct VirtIOVhostUserPCI, vdev);
    VirtioVhostUserPCIClass *vvup_class = VIRTIO_VHOST_USER_PCI_GET_CLASS(vvup);

    vvup_class->unregister_doorbell(vvup, e, vq_idx);
}

static uint64_t virtio_vhost_user_notification_read(void *opaque, hwaddr addr,
                                               unsigned size)
{
    VirtIOVhostUserPCI *vvup = opaque;
    VirtIOVhostUser *s = &vvup->vdev;
    uint64_t val = 0;

    switch (addr) {
    case NOTIFICATION_SELECT:
           val = s->nselect;
           break;
    case NOTIFICATION_MSIX_VECTOR:
            if (s->nselect < ARRAY_SIZE(s->kickfds)) {
                val = s->kickfds[s->nselect].msi_vector;
            }
           break;
    default:
           break;
    }

    trace_virtio_vhost_user_notification_read(s, addr, val);

    return val;
}

/* Set the MSI vectors for the frontend virtqueue notifications. */
static void virtio_vhost_user_notification_write(void *opaque, hwaddr addr,
                                               uint64_t val, unsigned size)
{
   /*
    * MMIO regions are byte-addressable. The value of the `addr` argument is
    * relative to the starting address of the MMIO region. For example,
    * `addr = 6` means that the 6th byte of this MMIO region has been written.
    */
    VirtIOVhostUserPCI *vvup = opaque;
    VirtIOPCIProxy *proxy = &vvup->parent_obj;
    VirtIOVhostUser *s = &vvup->vdev;

    switch (addr) {
    case NOTIFICATION_SELECT:
       if (val < VIRTIO_QUEUE_MAX) {
            s->nselect = val;
       }
       break;
    case NOTIFICATION_MSIX_VECTOR:
        msix_vector_unuse(&proxy->pci_dev, s->kickfds[s->nselect].msi_vector);
        if (msix_vector_use(&proxy->pci_dev, val) < 0) {
            val = VIRTIO_NO_VECTOR;
        }
        s->kickfds[s->nselect].msi_vector = val;
       break;
    default:
        break;
    }

    trace_virtio_vhost_user_notification_write(s, addr, val);
}

/*
 * Add the shared memory region as a subregion of the
 * additional_resources_bar.
 */
static void vvu_set_vhost_mem_regions(VirtIOVhostUserPCI *vvup)
{
    VirtIOVhostUser *s = &vvup->vdev;
    VhostUserMemory m = s->read_msg.payload.memory, *memory = &m;
    hwaddr subregion_offset;
    uint32_t i;

    /* Start after the notification structure */
    subregion_offset = vvup->shared_memory.offset;

    for (i = 0; i < memory->nregions; i++) {
        VirtIOVhostUserMemTableRegion *region = &s->mem_table[i];

        memory_region_init_ram_device_ptr(&region->mr, OBJECT(vvup),
                "virtio-vhost-user-mem-table-region",
                region->total_size, region->mmap_addr);
        memory_region_add_subregion(&vvup->additional_resources_bar,
                                    subregion_offset, &region->mr);

        subregion_offset += region->total_size;
    }
}

void virtio_vhost_user_set_vhost_mem_regions(VirtIOVhostUser *s)
{
    VirtIOVhostUserPCI *vvup = container_of(s, struct VirtIOVhostUserPCI, vdev);
    VirtioVhostUserPCIClass *vvup_class = VIRTIO_VHOST_USER_PCI_GET_CLASS(vvup);

    vvup_class->set_vhost_mem_regions(vvup);
}

static void vvu_delete_vhost_mem_region(VirtIOVhostUserPCI *vvup,
    MemoryRegion *mr)
{
    memory_region_del_subregion(&vvup->additional_resources_bar, mr);
    object_unparent(OBJECT(mr));
}


void virtio_vhost_user_delete_vhost_mem_region(VirtIOVhostUser *s,
    MemoryRegion *mr)
{
    VirtIOVhostUserPCI *vvup = container_of(s, struct VirtIOVhostUserPCI, vdev);
    VirtioVhostUserPCIClass *vvup_class = VIRTIO_VHOST_USER_PCI_GET_CLASS(vvup);

    vvup_class->delete_vhost_mem_region(vvup, mr);
}

static void virtio_vhost_user_init_bar(VirtIOVhostUserPCI *vvup)
{
    /* virtio-pci doesn't use BAR 2 & 3, so we use it */
    const int bar_index = 2;

    /*
     * TODO If the BAR is too large the guest won't have address space to map
     * it!
     */
    const uint64_t bar_size = 1ULL << 36;

    memory_region_init(&vvup->additional_resources_bar, OBJECT(vvup),
                       "virtio-vhost-user", bar_size);
    pci_register_bar(&vvup->parent_obj.pci_dev, bar_index,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &vvup->additional_resources_bar);

    /*
     * Initialize the VirtIOPCIRegions for the virtio configuration structures
     * corresponding to the additional device resource capabilities.
     * Place the additional device resources in the additional_resources_bar.
     */
    VirtIOPCIProxy *proxy = VIRTIO_PCI(vvup);

    vvup->doorbells.offset = 0x0;
    /* VIRTIO_QUEUE_MAX + 1 for logfd */
    vvup->doorbells.size = virtio_pci_queue_mem_mult(proxy)
        * (VIRTIO_QUEUE_MAX + 1);
    /* TODO Not sure if it is necessary for the size to be aligned */
    vvup->doorbells.size = QEMU_ALIGN_UP(vvup->doorbells.size, 4096);
    vvup->doorbells.type = VIRTIO_PCI_CAP_DOORBELL_CFG;

    vvup->notifications.offset = vvup->doorbells.offset + vvup->doorbells.size;
    vvup->notifications.size = 0x1000;
    vvup->notifications.type = VIRTIO_PCI_CAP_NOTIFICATION_CFG;

    /* cap.offset and cap.length must be 4096-byte (0x1000) aligned. */
    vvup->shared_memory.offset = vvup->notifications.offset
        + vvup->notifications.size;
    vvup->shared_memory.offset = QEMU_ALIGN_UP(vvup->shared_memory.offset,
        4096);
    /* TODO Reconsider the shared memory cap.length later */
    /*
     * The size of the shared memory region in the additional resources BAR
     * doesn't fit into the length field (uint32_t) of the virtio capability
     * structure. However, we don't need to pass this information to the guest
     * driver via the shared memory capability because the guest can figure out
     * the length of the vhost memory regions from the SET_MEM_TABLE vhost-user
     * messages. Therefore, the size of the shared memory region that we are
     * declaring here has no meaning and the guest driver shouldn't rely on
     * this.
     */
    vvup->shared_memory.size = 0x1000;
    vvup->shared_memory.type = VIRTIO_PCI_CAP_SHARED_MEMORY_CFG;

    /*
     * Initialize the MMIO MemoryRegions for the additional device
     *resources.
     */

    const struct MemoryRegionOps doorbell_ops = {
        .read = virtio_vhost_user_doorbells_read,
        .write = virtio_vhost_user_doorbells_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };

    const struct MemoryRegionOps notification_ops = {
        .read = virtio_vhost_user_notification_read,
        .write = virtio_vhost_user_notification_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };

    memory_region_init_io(&vvup->doorbells.mr, OBJECT(vvup),
                   &doorbell_ops, vvup, "virtio-vhost-user-doorbell-cfg",
                   vvup->doorbells.size);

    memory_region_init_io(&vvup->notifications.mr, OBJECT(vvup),
                    &notification_ops, vvup,
                    "virtio-vhost-user-notification-cfg",
                    vvup->notifications.size);

    /*
     * Register the virtio PCI configuration structures
     * for the additional device resources. This involves
     * registering the corresponding MemoryRegions as
     * subregions of the additional_resources_bar and creating
     * virtio capabilities.
     */
    struct virtio_pci_cap cap = {
        .cap_len = sizeof cap,
    };
    struct virtio_pci_doorbell_cap doorbell = {
        .cap.cap_len = sizeof doorbell,
        .doorbell_off_multiplier =
            cpu_to_le32(virtio_pci_queue_mem_mult(proxy)),
    };
    virtio_pci_modern_region_map(proxy, &vvup->doorbells, &doorbell.cap,
                                 &vvup->additional_resources_bar, bar_index);
    virtio_pci_modern_region_map(proxy, &vvup->notifications, &cap,
                                 &vvup->additional_resources_bar, bar_index);
    virtio_pci_modern_region_map(proxy, &vvup->shared_memory, &cap,
                                 &vvup->additional_resources_bar, bar_index);
}

static void vvu_cleanup_bar(VirtIOVhostUserPCI *vvup)
{
    memory_region_del_subregion(&vvup->additional_resources_bar,
                                &vvup->doorbells.mr);
    memory_region_del_subregion(&vvup->additional_resources_bar,
                                &vvup->notifications.mr);
}

void virtio_vhost_user_cleanup_additional_resources(VirtIOVhostUser *s)
{
    VirtIOVhostUserPCI *vvup = container_of(s, struct VirtIOVhostUserPCI, vdev);
    VirtioVhostUserPCIClass *vvup_class = VIRTIO_VHOST_USER_PCI_GET_CLASS(vvup);

    vvup_class->cleanup_bar(vvup);
}

static void virtio_vhost_user_pci_realize(VirtIOPCIProxy *vpci_dev,
                                          Error **errp)
{
    VirtIOVhostUserPCI *vvup = VIRTIO_VHOST_USER_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vvup->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = VIRTIO_QUEUE_MAX + 3;
    }

    virtio_vhost_user_init_bar(vvup);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus), &error_abort);
    object_property_set_bool(OBJECT(vdev), "realized", true, errp);
}

static void virtio_vhost_user_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    VirtioVhostUserPCIClass *vvup_class = VIRTIO_VHOST_USER_PCI_CLASS(klass);

    device_class_set_props(dc, virtio_vhost_user_pci_properties);
    k->realize = virtio_vhost_user_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_VHOST_USER;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;

    vvup_class->set_vhost_mem_regions = vvu_set_vhost_mem_regions;
    vvup_class->delete_vhost_mem_region = vvu_delete_vhost_mem_region;
    vvup_class->cleanup_bar = vvu_cleanup_bar;
    vvup_class->register_doorbell = vvu_register_doorbell;
    vvup_class->unregister_doorbell = vvu_unregister_doorbell;
}

static void virtio_vhost_user_pci_initfn(Object *obj)
{
    VirtIOVhostUserPCI *dev = VIRTIO_VHOST_USER_PCI(obj);

     virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_VHOST_USER);
}

static const VirtioPCIDeviceTypeInfo virtio_vhost_user_pci_info = {
    .base_name     = TYPE_VIRTIO_VHOST_USER_PCI,
    .generic_name  = "virtio-vhost-user-pci",
    .instance_size = sizeof(VirtIOVhostUserPCI),
    .instance_init = virtio_vhost_user_pci_initfn,
    .class_size    = sizeof(VirtioVhostUserPCIClass),
    .class_init    = virtio_vhost_user_pci_class_init,
};

static void virtio_vhost_user_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_vhost_user_pci_info);
}

type_init(virtio_vhost_user_pci_register_types);
