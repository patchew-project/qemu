/*
 * vhost-vfio
 *
 *  Copyright(c) 2017-2018 Intel Corporation. All rights reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <linux/vhost.h>
#include <linux/vfio.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/vhost-vfio.h"

// XXX: move to linux/vhost.h
struct vhost_vfio_op {
    __u64 request;
#define VHOST_VFIO_NEED_REPLY 0x1 /* Whether need reply */
    __u32 flags;
    __u32 size;
    union {
        __u64 u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        struct vhost_memory memory;
    } payload;
};
#define VHOST_VFIO_OP_HDR_SIZE (offsetof(struct vhost_vfio_op, payload))
// -- end here

// XXX: to be removed
#include <linux/kvm.h>
#include "sysemu/kvm.h"
extern int vfio_kvm_device_fd;

static int vhost_vfio_kvm_add_vfio_group(VhostVFIO *v)
{
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_ADD,
        .addr = (uint64_t)(uintptr_t)&v->group_fd,
    };
    int ret;

again:
    if (vfio_kvm_device_fd < 0) {
        struct kvm_create_device cd = {
            .type = KVM_DEV_TYPE_VFIO,
        };

        ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd);
        if (ret < 0) {
            if (errno == EBUSY) {
                goto again;
            }
            return -1;
        }

        vfio_kvm_device_fd = cd.fd;
    }

    ret = ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        return -1;
    }

    kvm_irqchip_commit_routes(kvm_state);

    return 0;
}

static int vhost_vfio_kvm_del_vfio_group(VhostVFIO *v)
{
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_DEL,
        .addr = (uint64_t)(uintptr_t)&v->group_fd,
    };
    int ret;

    ret = ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0)
        return -1;

    return 0;
}
// -- end here

static int vhost_vfio_write(struct vhost_dev *dev, struct vhost_vfio_op *op)
{
    VhostVFIO *vfio = dev->opaque;
    int count = VHOST_VFIO_OP_HDR_SIZE + op->size;
    int ret;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VFIO);

    ret = pwrite64(vfio->device_fd, op, count, vfio->bar0_offset);
    if (ret != count) {
        return -1;
    }

    return 0;
}

static int vhost_vfio_read(struct vhost_dev *dev, struct vhost_vfio_op *op)
{
    VhostVFIO *vfio = dev->opaque;
    int count = VHOST_VFIO_OP_HDR_SIZE + op->size;
    uint64_t request = op->request;
    int ret;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VFIO);

    ret = pread64(vfio->device_fd, op, count, vfio->bar0_offset);
    if (ret < 0 || request != op->request || ret != count) {
        return -1;
    }

    return 0;
}

static int vhost_vfio_init(struct vhost_dev *dev, void *opaque)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VFIO);

    dev->opaque = opaque;
    vhost_vfio_kvm_add_vfio_group(opaque);

    return 0;
}

static int vhost_vfio_cleanup(struct vhost_dev *dev)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VFIO);

    vhost_vfio_kvm_del_vfio_group(dev->opaque);
    dev->opaque = NULL;

    return 0;
}

static int vhost_vfio_memslots_limit(struct vhost_dev *dev)
{
    int limit = 64; // XXX hardcoded for now

    return limit;
}

static int vhost_vfio_set_log_base(struct vhost_dev *dev, uint64_t base,
                                   struct vhost_log *log)
{
    struct vhost_vfio_op op;

    op.request = VHOST_SET_LOG_BASE;
    op.flags = 0;
    op.size = sizeof(base);
    op.payload.u64 = base;

    return vhost_vfio_write(dev, &op);
}

// XXX: When IOMMU support mdev bus, we can use VFIO API to set up DMA mapping.
static int vhost_vfio_set_mem_table(struct vhost_dev *dev,
                                    struct vhost_memory *mem)
{
    struct vhost_vfio_op *op;
    uint32_t size = sizeof(*mem) + mem->nregions * sizeof(*mem->regions);
    int ret;

    if (mem->padding)
        return -1;

    op = g_malloc0(VHOST_VFIO_OP_HDR_SIZE + size);

    op->request = VHOST_SET_MEM_TABLE;
    op->flags = 0;
    op->size = size;
    memcpy(&op->payload.memory, mem, size);

    ret = vhost_vfio_write(dev, op);

    free(op);

    return ret;
}

// XXX: Pass IOVA addr directly when DMA mapping programmed by QEMU.
static int vhost_vfio_set_vring_addr(struct vhost_dev *dev,
                                     struct vhost_vring_addr *addr)
{
    struct vhost_vfio_op op;

    op.request = VHOST_SET_VRING_ADDR;
    op.flags = 0;
    op.size = sizeof(*addr);
    op.payload.addr = *addr;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_set_vring_num(struct vhost_dev *dev,
                                    struct vhost_vring_state *ring)
{
    struct vhost_vfio_op op;

    op.request = VHOST_SET_VRING_NUM;
    op.flags = 0;
    op.size = sizeof(*ring);
    op.payload.state = *ring;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_set_vring_base(struct vhost_dev *dev,
                                     struct vhost_vring_state *ring)
{
    struct vhost_vfio_op op;

    op.request = VHOST_SET_VRING_BASE;
    op.flags = 0;
    op.size = sizeof(*ring);
    op.payload.state = *ring;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_get_vring_base(struct vhost_dev *dev,
                                     struct vhost_vring_state *ring)
{
    struct vhost_vfio_op op;
    int ret;

    op.request = VHOST_GET_VRING_BASE;
    op.flags = VHOST_VFIO_NEED_REPLY;
    op.payload.state = *ring;
    op.size = sizeof(op.payload.state);

    ret = vhost_vfio_write(dev, &op);
    if (ret != 0)
        goto out;

    op.request = VHOST_GET_VRING_BASE;
    op.flags = 0;
    op.size = sizeof(*ring);

    ret = vhost_vfio_read(dev, &op);
    if (ret != 0)
        goto out;

    *ring = op.payload.state;

out:
    return ret;
}

static void notify_relay(void *opaque)
{
    size_t page_size = qemu_real_host_page_size;
    struct VhostVFIONotifyCtx *ctx = opaque;
    VhostVFIO *vfio = container_of(ctx, VhostVFIO, notify[ctx->qid]);
    int offset = page_size * ctx->qid;
    eventfd_t value;
    int ret;

    eventfd_read(ctx->kick_fd, &value);

    /* For virtio 0.95 case, no EPT mapping, QEMU MMIO write to help the notify relay */
    if (ctx->addr) {
        *((uint16_t *)ctx->addr) = ctx->qid;
        return;
    }

    /* If the device BAR is not mmap-able, write device fd for notify */
    ret = pwrite64(vfio->device_fd, &ctx->qid, sizeof(ctx->qid),
             vfio->bar1_offset + offset);
    if (ret < 0) {
        // XXX: error handling (e.g. unset the handler, report error, etc.)
    }
}

static int vhost_vfio_set_vring_kick(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    size_t page_size = qemu_real_host_page_size;
    VirtIODevice *vdev = dev->vdev;
    VhostVFIO *vfio = dev->opaque;
    VhostVFIONotifyCtx *ctx;
    int queue_idx;
    char *name;
    void *addr;

    queue_idx = file->index + dev->vq_index;
    ctx = &vfio->notify[queue_idx];
    ctx->qid = queue_idx;

    if (ctx->kick_fd > 0) {
        qemu_set_fd_handler(ctx->kick_fd, NULL, NULL, NULL);
        ctx->kick_fd = -1;

        if (ctx->addr) {
            virtio_queue_set_host_notifier_mr(vdev, queue_idx, &ctx->mr, false);
            object_unparent(OBJECT(&ctx->mr));
            munmap(ctx->addr, page_size);
            ctx->addr = NULL;
        }
    }

    if (file->fd <= 0)
        return 0;

    ctx->kick_fd = file->fd;

    qemu_set_fd_handler(file->fd, notify_relay, NULL, ctx);

    addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                vfio->device_fd, vfio->bar1_offset + page_size * queue_idx);
    /* It's okay to mmap fail, but would expect lower performance */
    if (addr == MAP_FAILED)
        return 0;

    name = g_strdup_printf("vhost-vfio/notifier@%p[%d]", vfio, queue_idx);
    memory_region_init_ram_device_ptr(&ctx->mr, OBJECT(vdev), name, page_size, addr);
    g_free(name);
    ctx->addr = addr;

    virtio_queue_set_host_notifier_mr(vdev, queue_idx, &ctx->mr, true);
    return 0;
}

#define IRQ_SET_BUF_LEN (sizeof(struct vfio_irq_set) + sizeof(int) * 1)

static int vhost_vfio_set_vring_call(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    VhostVFIO *vfio = dev->opaque;
    struct vfio_irq_set *irq_set;
    char irq_set_buf[IRQ_SET_BUF_LEN];
    int *fd_ptr;
    int ret;

    irq_set = (struct vfio_irq_set *)irq_set_buf;
    irq_set->flags = VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq_set->start = file->index;

    if (file->fd == -1) {
        irq_set->argsz = sizeof(struct vfio_irq_set);
        irq_set->count = 0;
        irq_set->flags |= VFIO_IRQ_SET_DATA_NONE;
    } else {
        irq_set->argsz = sizeof(irq_set_buf);
        irq_set->count = 1;
        irq_set->flags |= VFIO_IRQ_SET_DATA_EVENTFD;
        fd_ptr = (int *)&irq_set->data;
        fd_ptr[0] = file->fd;
    }

    ret = ioctl(vfio->device_fd, VFIO_DEVICE_SET_IRQS, irq_set);

    return ret;
}

static int vhost_vfio_set_features(struct vhost_dev *dev,
                                   uint64_t features)
{
    struct vhost_vfio_op op;

    op.request = VHOST_SET_FEATURES;
    op.flags = 0;
    op.size = sizeof(features);
    op.payload.u64 = features;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_get_features(struct vhost_dev *dev,
                                   uint64_t *features)
{
    struct vhost_vfio_op op;
    int ret;

    op.request = VHOST_GET_FEATURES;
    op.flags = VHOST_VFIO_NEED_REPLY;
    op.size = 0;

    ret = vhost_vfio_write(dev, &op);
    if (ret != 0)
        goto out;

    op.request = VHOST_GET_FEATURES;
    op.flags = 0;
    op.size = sizeof(*features);

    ret = vhost_vfio_read(dev, &op);
    if (ret != 0)
        goto out;

    *features = op.payload.u64;
out:
    return ret;
}

static int vhost_vfio_set_owner(struct vhost_dev *dev)
{
    struct vhost_vfio_op op;

    op.request = VHOST_SET_OWNER;
    op.flags = 0;
    op.size = 0;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_reset_device(struct vhost_dev *dev)
{
    struct vhost_vfio_op op;

    op.request = VHOST_RESET_OWNER;
    op.flags = 0;
    op.size = 0;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_get_vq_index(struct vhost_dev *dev, int idx)
{
    assert(idx >= dev->vq_index && idx < dev->vq_index + dev->nvqs);

    return idx - dev->vq_index;
}

static int vhost_vfio_set_state(struct vhost_dev *dev, int state)
{
    struct vhost_vfio_op op;

    op.request = VHOST_DEVICE_SET_STATE;
    op.flags = 0;
    op.size = sizeof(state);
    op.payload.u64 = state;

    return vhost_vfio_write(dev, &op);
}

static int vhost_vfio_migration_done(struct vhost_dev *dev, char* mac_addr)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VFIO);

    /* If guest supports GUEST_ANNOUNCE, do nothing */
    if (virtio_has_feature(dev->acked_features, VIRTIO_NET_F_GUEST_ANNOUNCE)) {
        return 0;
    }

    return -1;
}

static bool vhost_vfio_mem_section_filter(struct vhost_dev *dev,
                                          MemoryRegionSection *section)
{
    bool result;

    result = memory_region_get_fd(section->mr) >= 0;

    return result;
}

const VhostOps vfio_ops = {
        .backend_type = VHOST_BACKEND_TYPE_VFIO,
        .vhost_backend_init = vhost_vfio_init,
        .vhost_backend_cleanup = vhost_vfio_cleanup,
        .vhost_backend_memslots_limit = vhost_vfio_memslots_limit,
        .vhost_set_log_base = vhost_vfio_set_log_base,
        .vhost_set_mem_table = vhost_vfio_set_mem_table,
        .vhost_set_vring_addr = vhost_vfio_set_vring_addr,
        .vhost_set_vring_endian = NULL,
        .vhost_set_vring_num = vhost_vfio_set_vring_num,
        .vhost_set_vring_base = vhost_vfio_set_vring_base,
        .vhost_get_vring_base = vhost_vfio_get_vring_base,
        .vhost_set_vring_kick = vhost_vfio_set_vring_kick,
        .vhost_set_vring_call = vhost_vfio_set_vring_call,
        .vhost_set_features = vhost_vfio_set_features,
        .vhost_get_features = vhost_vfio_get_features,
        .vhost_set_owner = vhost_vfio_set_owner,
        .vhost_reset_device = vhost_vfio_reset_device,
        .vhost_get_vq_index = vhost_vfio_get_vq_index,
        // XXX: implement this to support MQ
        .vhost_set_vring_enable = NULL,
        .vhost_requires_shm_log = NULL,
        .vhost_migration_done = vhost_vfio_migration_done,
        .vhost_backend_can_merge = NULL,
        .vhost_net_set_mtu = NULL,
        .vhost_set_iotlb_callback = NULL,
        .vhost_send_device_iotlb_msg = NULL,
        .vhost_backend_mem_section_filter = vhost_vfio_mem_section_filter,
        .vhost_set_state = vhost_vfio_set_state,
};
