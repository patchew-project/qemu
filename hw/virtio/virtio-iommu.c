/*
 * virtio-iommu device
 *
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu-common.h"
#include "hw/virtio/virtio.h"
#include "sysemu/kvm.h"
#include "qapi-event.h"
#include "qemu/error-report.h"
#include "trace.h"

#include "standard-headers/linux/virtio_ids.h"
#include <linux/virtio_iommu.h>

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci.h"

/* Max size */
#define VIOMMU_DEFAULT_QUEUE_SIZE 256

typedef struct viommu_as viommu_as;

typedef struct viommu_mapping {
    uint64_t virt_addr;
    uint64_t phys_addr;
    uint64_t size;
    uint32_t flags;
} viommu_mapping;

typedef struct viommu_interval {
    uint64_t low;
    uint64_t high;
} viommu_interval;

typedef struct viommu_dev {
    uint32_t id;
    viommu_as *as;
} viommu_dev;

struct viommu_as {
    uint32_t id;
    GTree *mappings;
};

static inline uint16_t virtio_iommu_get_sid(IOMMUDevice *dev)
{
    return PCI_BUILD_BDF(pci_bus_num(dev->bus), dev->devfn);
}

static AddressSpace *virtio_iommu_find_add_as(PCIBus *bus, void *opaque,
                                              int devfn)
{
    VirtIOIOMMU *s = opaque;
    uintptr_t key = (uintptr_t)bus;
    IOMMUPciBus *sbus = g_hash_table_lookup(s->as_by_busptr, &key);
    IOMMUDevice *sdev;

    if (!sbus) {
        uintptr_t *new_key = g_malloc(sizeof(*new_key));

        *new_key = (uintptr_t)bus;
        sbus = g_malloc0(sizeof(IOMMUPciBus) +
                         sizeof(IOMMUDevice *) * IOMMU_PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->as_by_busptr, new_key, sbus);
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        char *name = g_strdup_printf("%s-%d-%d",
                                     TYPE_VIRTIO_IOMMU_MEMORY_REGION,
                                     pci_bus_num(bus), devfn);
        sdev = sbus->pbdev[devfn] = g_malloc0(sizeof(IOMMUDevice));

        sdev->viommu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        memory_region_init_iommu(&sdev->iommu_mr, sizeof(sdev->iommu_mr),
                                 TYPE_VIRTIO_IOMMU_MEMORY_REGION,
                                 OBJECT(s), name,
                                 UINT64_MAX);
        address_space_init(&sdev->as,
                           MEMORY_REGION(&sdev->iommu_mr), TYPE_VIRTIO_IOMMU);
    }

    return &sdev->as;

}

static void virtio_iommu_init_as(VirtIOIOMMU *s)
{
    PCIBus *pcibus = pci_find_primary_bus();

    if (pcibus) {
        pci_setup_iommu(pcibus, virtio_iommu_find_add_as, s);
    } else {
        error_report("No PCI bus, virtio-iommu is not registered");
    }
}

static gint interval_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    viommu_interval *inta = (viommu_interval *)a;
    viommu_interval *intb = (viommu_interval *)b;

    if (inta->high <= intb->low) {
        return -1;
    } else if (intb->high <= inta->low) {
        return 1;
    } else {
        return 0;
    }
}

static void virtio_iommu_detach_dev(VirtIOIOMMU *s, viommu_dev *dev)
{
    viommu_as *as = dev->as;

    trace_virtio_iommu_detach(dev->id);

    g_tree_remove(s->devices, GUINT_TO_POINTER(dev->id));
    g_tree_unref(as->mappings);
}

static int virtio_iommu_attach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_attach *req)
{
    uint32_t asid = le32_to_cpu(req->address_space);
    uint32_t devid = le32_to_cpu(req->device);
    uint32_t reserved = le32_to_cpu(req->reserved);
    viommu_as *as;
    viommu_dev *dev;

    trace_virtio_iommu_attach(asid, devid);

    if (reserved) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    dev = g_tree_lookup(s->devices, GUINT_TO_POINTER(devid));
    if (dev) {
        /*
         * the device is already attached to an address space,
         * detach it first
         */
         virtio_iommu_detach_dev(s, dev);
    }

    as = g_tree_lookup(s->address_spaces, GUINT_TO_POINTER(asid));
    if (!as) {
        as = g_malloc0(sizeof(*as));
        as->id = asid;
        as->mappings = g_tree_new_full((GCompareDataFunc)interval_cmp,
                                         NULL, NULL, (GDestroyNotify)g_free);
        g_tree_insert(s->address_spaces, GUINT_TO_POINTER(asid), as);
        trace_virtio_iommu_new_asid(asid);
    }

    dev = g_malloc0(sizeof(*dev));
    dev->as = as;
    dev->id = devid;
    trace_virtio_iommu_new_devid(devid);
    g_tree_insert(s->devices, GUINT_TO_POINTER(devid), dev);
    g_tree_ref(as->mappings);

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_detach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_detach *req)
{
    uint32_t devid = le32_to_cpu(req->device);
    uint32_t reserved = le32_to_cpu(req->reserved);
    viommu_dev *dev;

    if (reserved) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    dev = g_tree_lookup(s->devices, GUINT_TO_POINTER(devid));
    if (!dev) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    virtio_iommu_detach_dev(s, dev);

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_map(VirtIOIOMMU *s,
                            struct virtio_iommu_req_map *req)
{
    uint32_t asid = le32_to_cpu(req->address_space);
    uint64_t phys_addr = le64_to_cpu(req->phys_addr);
    uint64_t virt_addr = le64_to_cpu(req->virt_addr);
    uint64_t size = le64_to_cpu(req->size);
    uint32_t flags = le32_to_cpu(req->flags);
    viommu_as *as;
    viommu_interval *interval;
    viommu_mapping *mapping;

    interval = g_malloc0(sizeof(*interval));

    interval->low = virt_addr;
    interval->high = virt_addr + size - 1;

    as = g_tree_lookup(s->address_spaces, GUINT_TO_POINTER(asid));
    if (!as) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    mapping = g_tree_lookup(as->mappings, (gpointer)interval);
    if (mapping) {
        g_free(interval);
        return VIRTIO_IOMMU_S_INVAL;
    }

    trace_virtio_iommu_map(asid, phys_addr, virt_addr, size, flags);

    mapping = g_malloc0(sizeof(*mapping));
    mapping->virt_addr = virt_addr;
    mapping->phys_addr = phys_addr;
    mapping->size = size;
    mapping->flags = flags;

    g_tree_insert(as->mappings, interval, mapping);

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_unmap(VirtIOIOMMU *s,
                              struct virtio_iommu_req_unmap *req)
{
    uint32_t asid = le32_to_cpu(req->address_space);
    uint64_t virt_addr = le64_to_cpu(req->virt_addr);
    uint64_t size = le64_to_cpu(req->size);
    uint32_t flags = le32_to_cpu(req->flags);
    viommu_mapping *mapping;
    viommu_interval interval;
    viommu_as *as;

    trace_virtio_iommu_unmap(asid, virt_addr, size, flags);

    as = g_tree_lookup(s->address_spaces, GUINT_TO_POINTER(asid));
    if (!as) {
        error_report("%s: no as", __func__);
        return VIRTIO_IOMMU_S_NOENT;
    }
    interval.low = virt_addr;
    interval.high = virt_addr + size - 1;

    mapping = g_tree_lookup(as->mappings, (gpointer)&interval);

    while (mapping) {
        viommu_interval current;
        uint64_t low  = mapping->virt_addr;
        uint64_t high = mapping->virt_addr + mapping->size - 1;

        current.low = low;
        current.high = high;

        if (low == interval.low && size >= mapping->size) {
            g_tree_remove(as->mappings, (gpointer)&current);
            interval.low = high + 1;
            trace_virtio_iommu_unmap_left_interval(current.low, current.high,
                interval.low, interval.high);
        } else if (high == interval.high && size >= mapping->size) {
            trace_virtio_iommu_unmap_right_interval(current.low, current.high,
                interval.low, interval.high);
            g_tree_remove(as->mappings, (gpointer)&current);
            interval.high = low - 1;
        } else if (low > interval.low && high < interval.high) {
            trace_virtio_iommu_unmap_inc_interval(current.low, current.high);
            g_tree_remove(as->mappings, (gpointer)&current);
        } else {
            break;
        }
        if (interval.low >= interval.high) {
            return VIRTIO_IOMMU_S_OK;
        } else {
            mapping = g_tree_lookup(as->mappings, (gpointer)&interval);
        }
    }

    if (mapping) {
        error_report("****** %s: Unmap 0x%"PRIx64" size=0x%"PRIx64
                     " from 0x%"PRIx64" size=0x%"PRIx64" is not supported",
                     __func__, interval.low, size,
                     mapping->virt_addr, mapping->size);
    } else {
        error_report("****** %s: no mapping for [0x%"PRIx64",0x%"PRIx64"]",
                     __func__, interval.low, interval.high);
    }

    return VIRTIO_IOMMU_S_INVAL;
}

#define get_payload_size(req) (\
sizeof((req)) - sizeof(struct virtio_iommu_req_tail))

static int virtio_iommu_handle_attach(VirtIOIOMMU *s,
                                      struct iovec *iov,
                                      unsigned int iov_cnt)
{
    struct virtio_iommu_req_attach req;
    size_t sz, payload_sz;

    payload_sz = get_payload_size(req);

    sz = iov_to_buf(iov, iov_cnt, 0, &req, payload_sz);
    if (sz != payload_sz) {
        return VIRTIO_IOMMU_S_INVAL;
    }
    return virtio_iommu_attach(s, &req);
}
static int virtio_iommu_handle_detach(VirtIOIOMMU *s,
                                      struct iovec *iov,
                                      unsigned int iov_cnt)
{
    struct virtio_iommu_req_detach req;
    size_t sz, payload_sz;

    payload_sz = get_payload_size(req);

    sz = iov_to_buf(iov, iov_cnt, 0, &req, payload_sz);
    if (sz != payload_sz) {
        return VIRTIO_IOMMU_S_INVAL;
    }
    return virtio_iommu_detach(s, &req);
}
static int virtio_iommu_handle_map(VirtIOIOMMU *s,
                                   struct iovec *iov,
                                   unsigned int iov_cnt)
{
    struct virtio_iommu_req_map req;
    size_t sz, payload_sz;

    payload_sz = get_payload_size(req);

    sz = iov_to_buf(iov, iov_cnt, 0, &req, payload_sz);
    if (sz != payload_sz) {
        return VIRTIO_IOMMU_S_INVAL;
    }
    return virtio_iommu_map(s, &req);
}
static int virtio_iommu_handle_unmap(VirtIOIOMMU *s,
                                     struct iovec *iov,
                                     unsigned int iov_cnt)
{
    struct virtio_iommu_req_unmap req;
    size_t sz, payload_sz;

    payload_sz = get_payload_size(req);

    sz = iov_to_buf(iov, iov_cnt, 0, &req, payload_sz);
    if (sz != payload_sz) {
        return VIRTIO_IOMMU_S_INVAL;
    }
    return virtio_iommu_unmap(s, &req);
}

static void virtio_iommu_handle_command(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOIOMMU *s = VIRTIO_IOMMU(vdev);
    VirtQueueElement *elem;
    struct virtio_iommu_req_head head;
    struct virtio_iommu_req_tail tail;
    unsigned int iov_cnt;
    struct iovec *iov;
    size_t sz;

    for (;;) {
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            return;
        }

        if (iov_size(elem->in_sg, elem->in_num) < sizeof(tail) ||
            iov_size(elem->out_sg, elem->out_num) < sizeof(head)) {
            virtio_error(vdev, "virtio-iommu erroneous head or tail");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        iov_cnt = elem->out_num;
        iov = g_memdup(elem->out_sg, sizeof(struct iovec) * elem->out_num);
        sz = iov_to_buf(iov, iov_cnt, 0, &head, sizeof(head));
        if (sz != sizeof(head)) {
            tail.status = VIRTIO_IOMMU_S_UNSUPP;
        }
        qemu_mutex_lock(&s->mutex);
        switch (head.type) {
        case VIRTIO_IOMMU_T_ATTACH:
            tail.status = virtio_iommu_handle_attach(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_DETACH:
            tail.status = virtio_iommu_handle_detach(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_MAP:
            tail.status = virtio_iommu_handle_map(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_UNMAP:
            tail.status = virtio_iommu_handle_unmap(s, iov, iov_cnt);
            break;
        default:
            tail.status = VIRTIO_IOMMU_S_UNSUPP;
        }
        qemu_mutex_unlock(&s->mutex);

        sz = iov_from_buf(elem->in_sg, elem->in_num, 0,
                          &tail, sizeof(tail));
        assert(sz == sizeof(tail));

        virtqueue_push(vq, elem, sizeof(tail));
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static IOMMUTLBEntry virtio_iommu_translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                            IOMMUAccessFlags flag)
{
    IOMMUDevice *sdev = container_of(mr, IOMMUDevice, iommu_mr);
    VirtIOIOMMU *s = sdev->viommu;
    uint32_t sid;
    viommu_dev *dev;
    viommu_mapping *mapping;
    viommu_interval interval;

    interval.low = addr;
    interval.high = addr + 1;

    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = (1 << 12) - 1, /* TODO */
        .perm = 3,
    };

    sid = virtio_iommu_get_sid(sdev);

    trace_virtio_iommu_translate(mr->parent_obj.name, sid, addr, flag);
    qemu_mutex_lock(&s->mutex);

    dev = g_tree_lookup(s->devices, GUINT_TO_POINTER(sid));
    if (!dev) {
        /* device cannot be attached to another as */
        printf("%s sid=%d is not known!!\n", __func__, sid);
        goto unlock;
    }

    mapping = g_tree_lookup(dev->as->mappings, (gpointer)&interval);
    if (!mapping) {
        printf("%s no mapping for 0x%"PRIx64" for sid=%d\n", __func__,
               addr, sid);
        goto unlock;
    }
    entry.translated_addr = addr - mapping->virt_addr + mapping->phys_addr,
    trace_virtio_iommu_translate_result(addr, entry.translated_addr, sid);

unlock:
    qemu_mutex_unlock(&s->mutex);
    return entry;
}

static void virtio_iommu_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);

    memcpy(config_data, &dev->config, sizeof(struct virtio_iommu_config));
}

static void virtio_iommu_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_config config;

    memcpy(&config, config_data, sizeof(struct virtio_iommu_config));

    dev->config.page_sizes = le64_to_cpu(config.page_sizes);
    dev->config.input_range.end = le64_to_cpu(config.input_range.end);
}

static uint64_t virtio_iommu_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);
    f |= dev->host_features;
    virtio_add_feature(&f, VIRTIO_RING_F_EVENT_IDX);
    virtio_add_feature(&f, VIRTIO_RING_F_INDIRECT_DESC);
    virtio_add_feature(&f, VIRTIO_IOMMU_F_INPUT_RANGE);
    virtio_add_feature(&f, VIRTIO_IOMMU_F_MAP_UNMAP);
    return f;
}

static int virtio_iommu_post_load_device(void *opaque, int version_id)
{
    return 0;
}

static const VMStateDescription vmstate_virtio_iommu_device = {
    .name = "virtio-iommu-device",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_iommu_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
};

/*****************************
 * Hash Table
 *****************************/

static inline gboolean as_uint64_equal(gconstpointer v1, gconstpointer v2)
{
    return *((const uint64_t *)v1) == *((const uint64_t *)v2);
}

static inline guint as_uint64_hash(gconstpointer v)
{
    return (guint)*(const uint64_t *)v;
}

static gint int_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    uint ua = GPOINTER_TO_UINT(a);
    uint ub = GPOINTER_TO_UINT(b);
    return (ua > ub) - (ua < ub);
}

static void virtio_iommu_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    virtio_init(vdev, "virtio-iommu", VIRTIO_ID_IOMMU,
                sizeof(struct virtio_iommu_config));

    s->vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE,
                             virtio_iommu_handle_command);

    s->config.page_sizes = TARGET_PAGE_MASK;
    s->config.input_range.end = -1UL;

    qemu_mutex_init(&s->mutex);

    memset(s->as_by_bus_num, 0, sizeof(s->as_by_bus_num));
    s->as_by_busptr = g_hash_table_new_full(as_uint64_hash,
                                            as_uint64_equal,
                                            g_free, g_free);

    s->address_spaces = g_tree_new_full((GCompareDataFunc)int_cmp,
                                         NULL, NULL, (GDestroyNotify)g_free);
    s->devices = g_tree_new_full((GCompareDataFunc)int_cmp,
                                         NULL, NULL, (GDestroyNotify)g_free);

    virtio_iommu_init_as(s);
}

static void virtio_iommu_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    g_tree_destroy(s->address_spaces);
    g_tree_destroy(s->devices);

    virtio_cleanup(vdev);
}

static void virtio_iommu_device_reset(VirtIODevice *vdev)
{
}

static void virtio_iommu_set_status(VirtIODevice *vdev, uint8_t status)
{
}

static void virtio_iommu_instance_init(Object *obj)
{
}

static const VMStateDescription vmstate_virtio_iommu = {
    .name = "virtio-iommu",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_iommu_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_iommu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_iommu_properties;
    dc->vmsd = &vmstate_virtio_iommu;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_iommu_device_realize;
    vdc->unrealize = virtio_iommu_device_unrealize;
    vdc->reset = virtio_iommu_device_reset;
    vdc->get_config = virtio_iommu_get_config;
    vdc->set_config = virtio_iommu_set_config;
    vdc->get_features = virtio_iommu_get_features;
    vdc->set_status = virtio_iommu_set_status;
    vdc->vmsd = &vmstate_virtio_iommu_device;
}

static void virtio_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = virtio_iommu_translate;
}

static const TypeInfo virtio_iommu_info = {
    .name = TYPE_VIRTIO_IOMMU,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOIOMMU),
    .instance_init = virtio_iommu_instance_init,
    .class_init = virtio_iommu_class_init,
};

static const TypeInfo virtio_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_VIRTIO_IOMMU_MEMORY_REGION,
    .class_init = virtio_iommu_memory_region_class_init,
};


static void virtio_register_types(void)
{
    type_register_static(&virtio_iommu_info);
    type_register_static(&virtio_iommu_memory_region_info);
}

type_init(virtio_register_types)
