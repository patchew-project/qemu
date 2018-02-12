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
#include "qemu/log.h"
#include "qemu/iov.h"
#include "qemu-common.h"
#include "hw/virtio/virtio.h"
#include "sysemu/kvm.h"
#include "qapi/error.h"
#include "qapi-event.h"
#include "qemu/error-report.h"
#include "hw/i386/pc.h"
#include "hw/arm/virt.h"
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

typedef struct viommu_domain {
    uint32_t id;
    GTree *mappings;
    QLIST_HEAD(, viommu_endpoint) endpoint_list;
} viommu_domain;

typedef struct viommu_endpoint {
    uint32_t id;
    viommu_domain *domain;
    QLIST_ENTRY(viommu_endpoint) next;
    VirtIOIOMMU *viommu;
} viommu_endpoint;

typedef struct viommu_interval {
    uint64_t low;
    uint64_t high;
} viommu_interval;

typedef struct viommu_mapping {
    uint64_t virt_addr;
    uint64_t phys_addr;
    uint64_t size;
    uint32_t flags;
} viommu_mapping;

static inline uint16_t virtio_iommu_get_sid(IOMMUDevice *dev)
{
    return PCI_BUILD_BDF(pci_bus_num(dev->bus), dev->devfn);
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

static void virtio_iommu_detach_endpoint_from_domain(viommu_endpoint *ep)
{
    QLIST_REMOVE(ep, next);
    ep->domain = NULL;
}

static viommu_endpoint *virtio_iommu_get_endpoint(VirtIOIOMMU *s,
                                                  uint32_t ep_id)
{
    viommu_endpoint *ep;

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep_id));
    if (ep) {
        return ep;
    }
    ep = g_malloc0(sizeof(*ep));
    ep->id = ep_id;
    ep->viommu = s;
    trace_virtio_iommu_get_endpoint(ep_id);
    g_tree_insert(s->endpoints, GUINT_TO_POINTER(ep_id), ep);
    return ep;
}

static void virtio_iommu_put_endpoint(gpointer data)
{
    viommu_endpoint *ep = (viommu_endpoint *)data;

    if (ep->domain) {
        virtio_iommu_detach_endpoint_from_domain(ep);
        g_tree_unref(ep->domain->mappings);
    }

    trace_virtio_iommu_put_endpoint(ep->id);
    g_free(ep);
}

static viommu_domain *virtio_iommu_get_domain(VirtIOIOMMU *s,
                                              uint32_t domain_id)
{
    viommu_domain *domain;

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (domain) {
        return domain;
    }
    domain = g_malloc0(sizeof(*domain));
    domain->id = domain_id;
    domain->mappings = g_tree_new_full((GCompareDataFunc)interval_cmp,
                                   NULL, (GDestroyNotify)g_free,
                                   (GDestroyNotify)g_free);
    g_tree_insert(s->domains, GUINT_TO_POINTER(domain_id), domain);
    QLIST_INIT(&domain->endpoint_list);
    trace_virtio_iommu_get_domain(domain_id);
    return domain;
}

static void virtio_iommu_put_domain(gpointer data)
{
    viommu_domain *domain = (viommu_domain *)data;
    viommu_endpoint *iter, *tmp;

    QLIST_FOREACH_SAFE(iter, &domain->endpoint_list, next, tmp) {
        virtio_iommu_detach_endpoint_from_domain(iter);
    }
    g_tree_destroy(domain->mappings);
    trace_virtio_iommu_put_domain(domain->id);
    g_free(domain);
}

static AddressSpace *virtio_iommu_find_add_as(PCIBus *bus, void *opaque,
                                              int devfn)
{
    VirtIOIOMMU *s = opaque;
    IOMMUPciBus *sbus = g_hash_table_lookup(s->as_by_busptr, bus);
    IOMMUDevice *sdev;

    if (!sbus) {
        sbus = g_malloc0(sizeof(IOMMUPciBus) +
                         sizeof(IOMMUDevice *) * IOMMU_PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->as_by_busptr, bus, sbus);
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

        virtio_iommu_get_endpoint(s, PCI_BUILD_BDF(pci_bus_num(bus), devfn));

        trace_virtio_iommu_init_iommu_mr(name);

        memory_region_init_iommu(&sdev->iommu_mr, sizeof(sdev->iommu_mr),
                                 TYPE_VIRTIO_IOMMU_MEMORY_REGION,
                                 OBJECT(s), name,
                                 UINT64_MAX);
        address_space_init(&sdev->as,
                           MEMORY_REGION(&sdev->iommu_mr), TYPE_VIRTIO_IOMMU);
    }

    return &sdev->as;

}

static int virtio_iommu_attach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_attach *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint32_t ep_id = le32_to_cpu(req->endpoint);
    uint32_t reserved = le32_to_cpu(req->reserved);
    viommu_domain *domain;
    viommu_endpoint *ep;

    trace_virtio_iommu_attach(domain_id, ep_id);

    if (reserved) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    ep = virtio_iommu_get_endpoint(s, ep_id);
    if (ep->domain) {
        /*
         * the device is already attached to a domain,
         * detach it first
         */
        virtio_iommu_detach_endpoint_from_domain(ep);
    }

    domain = virtio_iommu_get_domain(s, domain_id);
    QLIST_INSERT_HEAD(&domain->endpoint_list, ep, next);

    ep->domain = domain;
    g_tree_ref(domain->mappings);

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_detach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_detach *req)
{
    uint32_t ep_id = le32_to_cpu(req->endpoint);
    uint32_t reserved = le32_to_cpu(req->reserved);
    viommu_endpoint *ep;

    if (reserved) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep_id));
    if (!ep) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    if (!ep->domain) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    virtio_iommu_detach_endpoint_from_domain(ep);
    trace_virtio_iommu_detach(ep_id);
    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_map(VirtIOIOMMU *s,
                            struct virtio_iommu_req_map *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t phys_start = le64_to_cpu(req->phys_start);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);
    uint32_t flags = le32_to_cpu(req->flags);
    viommu_domain *domain;
    viommu_interval *interval;
    viommu_mapping *mapping;

    interval = g_malloc0(sizeof(*interval));

    interval->low = virt_start;
    interval->high = virt_end;

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (!domain) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    mapping = g_tree_lookup(domain->mappings, (gpointer)interval);
    if (mapping) {
        g_free(interval);
        return VIRTIO_IOMMU_S_INVAL;
    }

    trace_virtio_iommu_map(domain_id, virt_start, virt_end, phys_start, flags);

    mapping = g_malloc0(sizeof(*mapping));
    mapping->virt_addr = virt_start;
    mapping->phys_addr = phys_start;
    mapping->size = virt_end - virt_start + 1;
    mapping->flags = flags;

    g_tree_insert(domain->mappings, interval, mapping);

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_unmap(VirtIOIOMMU *s,
                              struct virtio_iommu_req_unmap *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);
    uint64_t size = virt_end - virt_start + 1;
    viommu_mapping *mapping;
    viommu_interval interval;
    viommu_domain *domain;

    trace_virtio_iommu_unmap(domain_id, virt_start, virt_end);

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (!domain) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no domain\n", __func__);
        return VIRTIO_IOMMU_S_NOENT;
    }
    interval.low = virt_start;
    interval.high = virt_end;

    mapping = g_tree_lookup(domain->mappings, (gpointer)(&interval));

    while (mapping) {
        viommu_interval current;
        uint64_t low  = mapping->virt_addr;
        uint64_t high = mapping->virt_addr + mapping->size - 1;

        current.low = low;
        current.high = high;

        if (low == interval.low && size >= mapping->size) {
            g_tree_remove(domain->mappings, (gpointer)(&current));
            interval.low = high + 1;
            trace_virtio_iommu_unmap_left_interval(current.low, current.high,
                interval.low, interval.high);
        } else if (high == interval.high && size >= mapping->size) {
            trace_virtio_iommu_unmap_right_interval(current.low, current.high,
                interval.low, interval.high);
            g_tree_remove(domain->mappings, (gpointer)(&current));
            interval.high = low - 1;
        } else if (low > interval.low && high < interval.high) {
            trace_virtio_iommu_unmap_inc_interval(current.low, current.high);
            g_tree_remove(domain->mappings, (gpointer)(&current));
        } else {
            break;
        }
        if (interval.low >= interval.high) {
            return VIRTIO_IOMMU_S_OK;
        } else {
            mapping = g_tree_lookup(domain->mappings, (gpointer)(&interval));
        }
    }

    if (mapping) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "****** %s: Unmap 0x%"PRIx64" size=0x%"PRIx64
                     " from 0x%"PRIx64" size=0x%"PRIx64" is not supported\n",
                     __func__, interval.low, size,
                     mapping->virt_addr, mapping->size);
    } else {
        return VIRTIO_IOMMU_S_OK;
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
    uint32_t sid;

    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    sid = virtio_iommu_get_sid(sdev);

    trace_virtio_iommu_translate(mr->parent_obj.name, sid, addr, flag);
    return entry;
}

static void virtio_iommu_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_config *config = &dev->config;

    trace_virtio_iommu_get_config(config->page_size_mask,
                                  config->input_range.start,
                                  config->input_range.end,
                                  config->domain_bits,
                                  config->probe_size);
    memcpy(config_data, &dev->config, sizeof(struct virtio_iommu_config));
}

static void virtio_iommu_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
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

static void virtio_iommu_set_features(VirtIODevice *vdev, uint64_t val)
{
    trace_virtio_iommu_set_features(val);
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
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    PCIBus *pcibus;

    if (!mc->get_primary_pci_bus) {
        goto err;
    }
    pcibus = mc->get_primary_pci_bus(ms);
    if (!pcibus) {
        goto err;
    }

    virtio_init(vdev, "virtio-iommu", VIRTIO_ID_IOMMU,
                sizeof(struct virtio_iommu_config));

    s->req_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE,
                             virtio_iommu_handle_command);
    s->event_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE, NULL);

    s->config.page_size_mask = TARGET_PAGE_MASK;
    s->config.input_range.end = -1UL;

    qemu_mutex_init(&s->mutex);

    memset(s->as_by_bus_num, 0, sizeof(s->as_by_bus_num));
    s->as_by_busptr = g_hash_table_new(NULL, NULL);

    pci_setup_iommu(pcibus, virtio_iommu_find_add_as, s);

    s->domains = g_tree_new_full((GCompareDataFunc)int_cmp,
                                 NULL, NULL, virtio_iommu_put_domain);
    s->endpoints = g_tree_new_full((GCompareDataFunc)int_cmp,
                                   NULL, NULL, virtio_iommu_put_endpoint);
    return;
err:
    error_setg(&error_fatal, "virtio-iommu: no pci bus identified");
}

static void virtio_iommu_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    g_tree_destroy(s->domains);
    g_tree_destroy(s->endpoints);

    virtio_cleanup(vdev);
}

static void virtio_iommu_device_reset(VirtIODevice *vdev)
{
    trace_virtio_iommu_device_reset();
}

static void virtio_iommu_set_status(VirtIODevice *vdev, uint8_t status)
{
    trace_virtio_iommu_device_status(status);
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
    vdc->set_features = virtio_iommu_set_features;
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
