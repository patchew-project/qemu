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
#include "trace.h"

#include "standard-headers/linux/virtio_ids.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-iommu.h"

/* Max size */
#define VIOMMU_DEFAULT_QUEUE_SIZE 256

static int virtio_iommu_attach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_attach *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint32_t ep_id = le32_to_cpu(req->endpoint);

    trace_virtio_iommu_attach(domain_id, ep_id);

    return VIRTIO_IOMMU_S_UNSUPP;
}

static int virtio_iommu_detach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_detach *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint32_t ep_id = le32_to_cpu(req->endpoint);

    trace_virtio_iommu_detach(domain_id, ep_id);

    return VIRTIO_IOMMU_S_UNSUPP;
}

static int virtio_iommu_map(VirtIOIOMMU *s,
                            struct virtio_iommu_req_map *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t phys_start = le64_to_cpu(req->phys_start);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);
    uint32_t flags = le32_to_cpu(req->flags);

    trace_virtio_iommu_map(domain_id, virt_start, virt_end, phys_start, flags);

    return VIRTIO_IOMMU_S_UNSUPP;
}

static int virtio_iommu_unmap(VirtIOIOMMU *s,
                              struct virtio_iommu_req_unmap *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);

    trace_virtio_iommu_unmap(domain_id, virt_start, virt_end);

    return VIRTIO_IOMMU_S_UNSUPP;
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
    struct virtio_iommu_req_tail tail = {};
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
    struct virtio_iommu_config config;

    memcpy(&config, config_data, sizeof(struct virtio_iommu_config));
    trace_virtio_iommu_set_config(config.page_size_mask,
                                  config.input_range.start,
                                  config.input_range.end,
                                  config.domain_bits,
                                  config.probe_size);
}

static uint64_t virtio_iommu_get_features(VirtIODevice *vdev, uint64_t f,
                                          Error **errp)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);

    f |= dev->features;
    trace_virtio_iommu_get_features(f);
    return f;
}

static void virtio_iommu_set_features(VirtIODevice *vdev, uint64_t val)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);

    dev->acked_features = val;
    trace_virtio_iommu_set_features(dev->acked_features);
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

static void virtio_iommu_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    virtio_init(vdev, "virtio-iommu", VIRTIO_ID_IOMMU,
                sizeof(struct virtio_iommu_config));

    s->req_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE,
                             virtio_iommu_handle_command);
    s->event_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE, NULL);

    s->config.page_size_mask = TARGET_PAGE_MASK;
    s->config.input_range.end = -1UL;
    s->config.domain_bits = 32;

    virtio_add_feature(&s->features, VIRTIO_RING_F_EVENT_IDX);
    virtio_add_feature(&s->features, VIRTIO_RING_F_INDIRECT_DESC);
    virtio_add_feature(&s->features, VIRTIO_F_VERSION_1);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_INPUT_RANGE);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_DOMAIN_BITS);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_MAP_UNMAP);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_BYPASS);
}

static void virtio_iommu_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

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
    DEFINE_PROP_LINK("primary-bus", VirtIOIOMMU, primary_bus, "PCI", PCIBus *),
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

static const TypeInfo virtio_iommu_info = {
    .name = TYPE_VIRTIO_IOMMU,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOIOMMU),
    .instance_init = virtio_iommu_instance_init,
    .class_init = virtio_iommu_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_iommu_info);
}

type_init(virtio_register_types)
