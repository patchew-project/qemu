/*
 * vhost-user
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/vfio/vfio.h"
#include "chardev/char-fe.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/vhost.h>

#define VHOST_MEMORY_MAX_NREGIONS    8
#define VHOST_USER_F_PROTOCOL_FEATURES 30

enum VhostUserProtocolFeature {
    VHOST_USER_PROTOCOL_F_MQ = 0,
    VHOST_USER_PROTOCOL_F_LOG_SHMFD = 1,
    VHOST_USER_PROTOCOL_F_RARP = 2,
    VHOST_USER_PROTOCOL_F_REPLY_ACK = 3,
    VHOST_USER_PROTOCOL_F_NET_MTU = 4,
    VHOST_USER_PROTOCOL_F_SLAVE_REQ = 5,
    VHOST_USER_PROTOCOL_F_CROSS_ENDIAN = 6,
    VHOST_USER_PROTOCOL_F_VFIO = 7,

    VHOST_USER_PROTOCOL_F_MAX
};

#define VHOST_USER_PROTOCOL_FEATURE_MASK ((1 << VHOST_USER_PROTOCOL_F_MAX) - 1)

typedef enum VhostUserRequest {
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_OWNER = 4,
    VHOST_USER_SET_MEM_TABLE = 5,
    VHOST_USER_SET_LOG_BASE = 6,
    VHOST_USER_SET_LOG_FD = 7,
    VHOST_USER_SET_VRING_NUM = 8,
    VHOST_USER_SET_VRING_ADDR = 9,
    VHOST_USER_SET_VRING_BASE = 10,
    VHOST_USER_GET_VRING_BASE = 11,
    VHOST_USER_SET_VRING_KICK = 12,
    VHOST_USER_SET_VRING_CALL = 13,
    VHOST_USER_SET_VRING_ERR = 14,
    VHOST_USER_GET_PROTOCOL_FEATURES = 15,
    VHOST_USER_SET_PROTOCOL_FEATURES = 16,
    VHOST_USER_GET_QUEUE_NUM = 17,
    VHOST_USER_SET_VRING_ENABLE = 18,
    VHOST_USER_SEND_RARP = 19,
    VHOST_USER_NET_SET_MTU = 20,
    VHOST_USER_SET_SLAVE_REQ_FD = 21,
    VHOST_USER_IOTLB_MSG = 22,
    VHOST_USER_SET_VRING_ENDIAN = 23,
    VHOST_USER_MAX
} VhostUserRequest;

typedef enum VhostUserSlaveRequest {
    VHOST_USER_SLAVE_NONE = 0,
    VHOST_USER_SLAVE_IOTLB_MSG = 1,
    VHOST_USER_SLAVE_VFIO_SET_VRING_GROUP_FD = 2,
    VHOST_USER_SLAVE_VFIO_SET_VRING_NOTIFY_AREA = 3,
    VHOST_USER_SLAVE_MAX
}  VhostUserSlaveRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_MAX_NREGIONS];
} VhostUserMemory;

typedef struct VhostUserLog {
    uint64_t mmap_size;
    uint64_t mmap_offset;
} VhostUserLog;

typedef struct VhostUserVringArea {
    uint64_t u64;
    uint64_t size;
    uint64_t offset;
} VhostUserVringArea;

typedef struct VhostUserMsg {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1<<2)
#define VHOST_USER_NEED_REPLY_MASK  (0x1 << 3)
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserLog log;
        struct vhost_iotlb_msg iotlb;
        VhostUserVringArea area;
    } payload;
} QEMU_PACKED VhostUserMsg;

static VhostUserMsg m __attribute__ ((unused));
#define VHOST_USER_HDR_SIZE (sizeof(m.request) \
                            + sizeof(m.flags) \
                            + sizeof(m.size))

#define VHOST_USER_PAYLOAD_SIZE (sizeof(m) - VHOST_USER_HDR_SIZE)

/* The version of the protocol we support */
#define VHOST_USER_VERSION    (0x1)

struct vhost_user {
    VhostUser *shared;
    int slave_fd;
};

static bool ioeventfd_enabled(void)
{
    return kvm_enabled() && kvm_eventfds_enabled();
}

static int vhost_user_read(struct vhost_dev *dev, VhostUserMsg *msg)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = &u->shared->chr;
    uint8_t *p = (uint8_t *) msg;
    int r, size = VHOST_USER_HDR_SIZE;

    r = qemu_chr_fe_read_all(chr, p, size);
    if (r != size) {
        error_report("Failed to read msg header. Read %d instead of %d."
                     " Original request %d.", r, size, msg->request);
        goto fail;
    }

    /* validate received flags */
    if (msg->flags != (VHOST_USER_REPLY_MASK | VHOST_USER_VERSION)) {
        error_report("Failed to read msg header."
                " Flags 0x%x instead of 0x%x.", msg->flags,
                VHOST_USER_REPLY_MASK | VHOST_USER_VERSION);
        goto fail;
    }

    /* validate message size is sane */
    if (msg->size > VHOST_USER_PAYLOAD_SIZE) {
        error_report("Failed to read msg header."
                " Size %d exceeds the maximum %zu.", msg->size,
                VHOST_USER_PAYLOAD_SIZE);
        goto fail;
    }

    if (msg->size) {
        p += VHOST_USER_HDR_SIZE;
        size = msg->size;
        r = qemu_chr_fe_read_all(chr, p, size);
        if (r != size) {
            error_report("Failed to read msg payload."
                         " Read %d instead of %d.", r, msg->size);
            goto fail;
        }
    }

    return 0;

fail:
    return -1;
}

static int process_message_reply(struct vhost_dev *dev,
                                 const VhostUserMsg *msg)
{
    VhostUserMsg msg_reply;

    if ((msg->flags & VHOST_USER_NEED_REPLY_MASK) == 0) {
        return 0;
    }

    if (vhost_user_read(dev, &msg_reply) < 0) {
        return -1;
    }

    if (msg_reply.request != msg->request) {
        error_report("Received unexpected msg type."
                     "Expected %d received %d",
                     msg->request, msg_reply.request);
        return -1;
    }

    return msg_reply.payload.u64 ? -1 : 0;
}

static bool vhost_user_one_time_request(VhostUserRequest request)
{
    switch (request) {
    case VHOST_USER_SET_OWNER:
    case VHOST_USER_RESET_OWNER:
    case VHOST_USER_SET_MEM_TABLE:
    case VHOST_USER_GET_QUEUE_NUM:
    case VHOST_USER_NET_SET_MTU:
        return true;
    default:
        return false;
    }
}

/* most non-init callers ignore the error */
static int vhost_user_write(struct vhost_dev *dev, VhostUserMsg *msg,
                            int *fds, int fd_num)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = &u->shared->chr;
    int ret, size = VHOST_USER_HDR_SIZE + msg->size;

    /*
     * For non-vring specific requests, like VHOST_USER_SET_MEM_TABLE,
     * we just need send it once in the first time. For later such
     * request, we just ignore it.
     */
    if (vhost_user_one_time_request(msg->request) && dev->vq_index != 0) {
        msg->flags &= ~VHOST_USER_NEED_REPLY_MASK;
        return 0;
    }

    if (qemu_chr_fe_set_msgfds(chr, fds, fd_num) < 0) {
        error_report("Failed to set msg fds.");
        return -1;
    }

    ret = qemu_chr_fe_write_all(chr, (const uint8_t *) msg, size);
    if (ret != size) {
        error_report("Failed to write msg."
                     " Wrote %d instead of %d.", ret, size);
        return -1;
    }

    return 0;
}

static int vhost_user_set_log_base(struct vhost_dev *dev, uint64_t base,
                                   struct vhost_log *log)
{
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    size_t fd_num = 0;
    bool shmfd = virtio_has_feature(dev->protocol_features,
                                    VHOST_USER_PROTOCOL_F_LOG_SHMFD);
    VhostUserMsg msg = {
        .request = VHOST_USER_SET_LOG_BASE,
        .flags = VHOST_USER_VERSION,
        .payload.log.mmap_size = log->size * sizeof(*(log->log)),
        .payload.log.mmap_offset = 0,
        .size = sizeof(msg.payload.log),
    };

    if (shmfd && log->fd != -1) {
        fds[fd_num++] = log->fd;
    }

    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return -1;
    }

    if (shmfd) {
        msg.size = 0;
        if (vhost_user_read(dev, &msg) < 0) {
            return -1;
        }

        if (msg.request != VHOST_USER_SET_LOG_BASE) {
            error_report("Received unexpected msg type. "
                         "Expected %d received %d",
                         VHOST_USER_SET_LOG_BASE, msg.request);
            return -1;
        }
    }

    return 0;
}

static int vhost_user_set_mem_table(struct vhost_dev *dev,
                                    struct vhost_memory *mem)
{
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    int i, fd;
    size_t fd_num = 0;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);

    VhostUserMsg msg = {
        .request = VHOST_USER_SET_MEM_TABLE,
        .flags = VHOST_USER_VERSION,
    };

    if (reply_supported) {
        msg.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    for (i = 0; i < dev->mem->nregions; ++i) {
        struct vhost_memory_region *reg = dev->mem->regions + i;
        ram_addr_t offset;
        MemoryRegion *mr;

        assert((uintptr_t)reg->userspace_addr == reg->userspace_addr);
        mr = memory_region_from_host((void *)(uintptr_t)reg->userspace_addr,
                                     &offset);
        fd = memory_region_get_fd(mr);
        if (fd > 0) {
            msg.payload.memory.regions[fd_num].userspace_addr = reg->userspace_addr;
            msg.payload.memory.regions[fd_num].memory_size  = reg->memory_size;
            msg.payload.memory.regions[fd_num].guest_phys_addr = reg->guest_phys_addr;
            msg.payload.memory.regions[fd_num].mmap_offset = offset;
            assert(fd_num < VHOST_MEMORY_MAX_NREGIONS);
            fds[fd_num++] = fd;
        }
    }

    msg.payload.memory.nregions = fd_num;

    if (!fd_num) {
        error_report("Failed initializing vhost-user memory map, "
                     "consider using -object memory-backend-file share=on");
        return -1;
    }

    msg.size = sizeof(msg.payload.memory.nregions);
    msg.size += sizeof(msg.payload.memory.padding);
    msg.size += fd_num * sizeof(VhostUserMemoryRegion);

    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return -1;
    }

    if (reply_supported) {
        return process_message_reply(dev, &msg);
    }

    return 0;
}

static int vhost_user_set_vring_addr(struct vhost_dev *dev,
                                     struct vhost_vring_addr *addr)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_SET_VRING_ADDR,
        .flags = VHOST_USER_VERSION,
        .payload.addr = *addr,
        .size = sizeof(msg.payload.addr),
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_user_set_vring_endian(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    bool cross_endian = virtio_has_feature(dev->protocol_features,
                                           VHOST_USER_PROTOCOL_F_CROSS_ENDIAN);
    VhostUserMsg msg = {
        .request = VHOST_USER_SET_VRING_ENDIAN,
        .flags = VHOST_USER_VERSION,
        .payload.state = *ring,
        .size = sizeof(msg.payload.state),
    };

    if (!cross_endian) {
        error_report("vhost-user trying to send unhandled ioctl");
        return -1;
    }

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_set_vring(struct vhost_dev *dev,
                           unsigned long int request,
                           struct vhost_vring_state *ring)
{
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
        .payload.state = *ring,
        .size = sizeof(msg.payload.state),
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_user_set_vring_num(struct vhost_dev *dev,
                                    struct vhost_vring_state *ring)
{
    return vhost_set_vring(dev, VHOST_USER_SET_VRING_NUM, ring);
}

static int vhost_user_set_vring_base(struct vhost_dev *dev,
                                     struct vhost_vring_state *ring)
{
    return vhost_set_vring(dev, VHOST_USER_SET_VRING_BASE, ring);
}

static int vhost_user_set_vring_enable(struct vhost_dev *dev, int enable)
{
    int i;

    if (!virtio_has_feature(dev->features, VHOST_USER_F_PROTOCOL_FEATURES)) {
        return -1;
    }

    for (i = 0; i < dev->nvqs; ++i) {
        struct vhost_vring_state state = {
            .index = dev->vq_index + i,
            .num   = enable,
        };

        vhost_set_vring(dev, VHOST_USER_SET_VRING_ENABLE, &state);
    }

    return 0;
}

static int vhost_user_get_vring_base(struct vhost_dev *dev,
                                     struct vhost_vring_state *ring)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_GET_VRING_BASE,
        .flags = VHOST_USER_VERSION,
        .payload.state = *ring,
        .size = sizeof(msg.payload.state),
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    if (vhost_user_read(dev, &msg) < 0) {
        return -1;
    }

    if (msg.request != VHOST_USER_GET_VRING_BASE) {
        error_report("Received unexpected msg type. Expected %d received %d",
                     VHOST_USER_GET_VRING_BASE, msg.request);
        return -1;
    }

    if (msg.size != sizeof(msg.payload.state)) {
        error_report("Received bad msg size.");
        return -1;
    }

    *ring = msg.payload.state;

    return 0;
}

static int vhost_set_vring_file(struct vhost_dev *dev,
                                VhostUserRequest request,
                                struct vhost_vring_file *file)
{
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    size_t fd_num = 0;
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
        .payload.u64 = file->index & VHOST_USER_VRING_IDX_MASK,
        .size = sizeof(msg.payload.u64),
    };

    if (ioeventfd_enabled() && file->fd > 0) {
        fds[fd_num++] = file->fd;
    } else {
        msg.payload.u64 |= VHOST_USER_VRING_NOFD_MASK;
    }

    if (vhost_user_write(dev, &msg, fds, fd_num) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_user_set_vring_kick(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_KICK, file);
}

static int vhost_user_set_vring_call(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    return vhost_set_vring_file(dev, VHOST_USER_SET_VRING_CALL, file);
}

static int vhost_user_set_u64(struct vhost_dev *dev, int request, uint64_t u64)
{
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
        .payload.u64 = u64,
        .size = sizeof(msg.payload.u64),
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_user_set_features(struct vhost_dev *dev,
                                   uint64_t features)
{
    return vhost_user_set_u64(dev, VHOST_USER_SET_FEATURES, features);
}

static int vhost_user_set_protocol_features(struct vhost_dev *dev,
                                            uint64_t features)
{
    return vhost_user_set_u64(dev, VHOST_USER_SET_PROTOCOL_FEATURES, features);
}

static int vhost_user_get_u64(struct vhost_dev *dev, int request, uint64_t *u64)
{
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
    };

    if (vhost_user_one_time_request(request) && dev->vq_index != 0) {
        return 0;
    }

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    if (vhost_user_read(dev, &msg) < 0) {
        return -1;
    }

    if (msg.request != request) {
        error_report("Received unexpected msg type. Expected %d received %d",
                     request, msg.request);
        return -1;
    }

    if (msg.size != sizeof(msg.payload.u64)) {
        error_report("Received bad msg size.");
        return -1;
    }

    *u64 = msg.payload.u64;

    return 0;
}

static int vhost_user_get_features(struct vhost_dev *dev, uint64_t *features)
{
    return vhost_user_get_u64(dev, VHOST_USER_GET_FEATURES, features);
}

static int vhost_user_set_owner(struct vhost_dev *dev)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_SET_OWNER,
        .flags = VHOST_USER_VERSION,
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

static int vhost_user_reset_device(struct vhost_dev *dev)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_RESET_OWNER,
        .flags = VHOST_USER_VERSION,
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

#ifdef CONFIG_KVM
static int vfio_group_fd_to_id(int group_fd)
{
    char linkname[PATH_MAX];
    char pathname[PATH_MAX];
    char *filename;
    int group_id, ret;

    snprintf(linkname, sizeof(linkname), "/proc/self/fd/%d", group_fd);

    ret = readlink(linkname, pathname, sizeof(pathname));
    if (ret < 0) {
        return -1;
    }

    filename = g_path_get_basename(pathname);
    group_id = atoi(filename);
    g_free(filename);

    return group_id;
}

static int vhost_user_kvm_add_vfio_group(struct vhost_dev *dev,
                                         int group_id, int group_fd)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_user_vfio_state *vfio = &u->shared->vfio;
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_ADD,
    };
    bool found = false;
    int i, ret;

    for (i = 0; i < vfio->nr_group; i++) {
        if (vfio->group[i].id == group_id) {
            found = true;
            break;
        }
    }

    if (found) {
        close(group_fd);
        vfio->group[i].refcnt++;
        return 0;
    }

    if (vfio->nr_group >= VIRTIO_QUEUE_MAX) {
        return -1;
    }

    vfio->group[i].id = group_id;
    vfio->group[i].fd = group_fd;
    vfio->group[i].refcnt = 1;

    attr.addr = (uint64_t)(uintptr_t)&vfio->group[i].fd;

again:
    /* XXX: improve this */
    if (vfio_kvm_device_fd < 0) {
        struct kvm_create_device cd = {
            .type = KVM_DEV_TYPE_VFIO,
        };

        ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd);
        if (ret < 0) {
            if (errno == EBUSY) {
                goto again;
            }
            error_report("Failed to create KVM VFIO device.");
            return -1;
        }

        vfio_kvm_device_fd = cd.fd;
    }

    ret = ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        error_report("Failed to add group %d to KVM VFIO device.",
                     group_id);
        return -1;
    }

    vfio->nr_group++;

    return 0;
}

static int vhost_user_kvm_del_vfio_group(struct vhost_dev *dev, int group_id)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_user_vfio_state *vfio = &u->shared->vfio;
    struct kvm_device_attr attr = {
        .group = KVM_DEV_VFIO_GROUP,
        .attr = KVM_DEV_VFIO_GROUP_DEL,
    };
    bool found = false;
    int i, ret;

    kvm_irqchip_commit_routes(kvm_state);

    for (i = 0; i < vfio->nr_group; i++) {
        if (vfio->group[i].id == group_id) {
            found = true;
            break;
        }
    }

    if (!found) {
        return 0;
    }

    vfio->group[i].refcnt--;

    if (vfio->group[i].refcnt == 0) {
        attr.addr = (uint64_t)(uintptr_t)&vfio->group[i].fd;
        ret = ioctl(vfio_kvm_device_fd, KVM_SET_DEVICE_ATTR, &attr);
        if (ret < 0) {
            error_report("Failed to remove group %d from KVM VFIO device.",
                    group_id);
            vfio->group[i].refcnt++;
            return -1;
        }

        close(vfio->group[i].fd);

        for (; i + 1 < vfio->nr_group; i++) {
            vfio->group[i] = vfio->group[i + 1];
        }
        vfio->nr_group--;
    }

    return 0;
}

static int vhost_user_handle_vfio_set_vring_group_fd(struct vhost_dev *dev,
                                                     uint64_t u64,
                                                     int group_fd)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_user_vfio_state *vfio = &u->shared->vfio;
    int qid = u64 & VHOST_USER_VRING_IDX_MASK;
    int group_id, nvqs, ret = 0;

    qemu_mutex_lock(&vfio->lock);

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_VFIO)) {
        ret = -1;
        goto out;
    }

    if (dev->vdev == NULL) {
        error_report("vhost_dev isn't available.");
        ret = -1;
        goto out;
    }

    nvqs = virtio_get_num_queues(dev->vdev);
    if (qid >= nvqs) {
        error_report("invalid queue index.");
        ret = -1;
        goto out;
    }

    if (u64 & VHOST_USER_VRING_NOFD_MASK) {
        group_id = vfio->group_id[qid];
        if (group_id != -1) {
            if (vhost_user_kvm_del_vfio_group(dev, group_id) < 0) {
                ret = -1;
                goto out;
            }
            vfio->group_id[qid] = -1;
        }
        goto out;
    }

    group_id = vfio_group_fd_to_id(group_fd);
    if (group_id == -1) {
        ret = -1;
        goto out;
    }

    if (vfio->group_id[qid] == group_id) {
        close(group_fd);
        goto out;
    }

    if (vfio->group_id[qid] != -1) {
        if (vhost_user_kvm_del_vfio_group(dev, vfio->group_id[qid]) < 0) {
            ret = -1;
            goto out;
        }
        vfio->group_id[qid] = -1;
    }

    if (vhost_user_kvm_add_vfio_group(dev, group_id, group_fd) < 0) {
        ret = -1;
        goto out;
    }
    vfio->group_id[qid] = group_id;

out:
    kvm_irqchip_commit_routes(kvm_state);
    qemu_mutex_unlock(&vfio->lock);

    if (ret != 0 && group_fd != -1) {
        close(group_fd);
    }

    return ret;
}
#else
static int vhost_user_handle_vfio_set_vring_group_fd(struct vhost_dev *dev,
                                                     uint64_t u64,
                                                     int group_fd)
{
    if (group_fd != -1) {
        close(group_fd);
    }

    return 0;
}
#endif

static int vhost_user_add_mapping(struct vhost_dev *dev, int qid, int fd,
                                  uint64_t size, uint64_t offset)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_user_vfio_state *vfio = &u->shared->vfio;
    MemoryRegion *sysmem = get_system_memory();
    VirtIONetPCI *d;
    VirtIOPCIProxy *proxy; /* XXX: handle non-PCI case */
    uint64_t paddr;
    void *addr;
    char *name;

    d = container_of(dev->vdev, VirtIONetPCI, vdev.parent_obj);
    proxy = &d->parent_obj;

    if ((proxy->flags & VIRTIO_PCI_FLAG_PAGE_PER_VQ) == 0 ||
        size != virtio_pci_queue_mem_mult(proxy)) {
        return -1;
    }

    addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
    if (addr == MAP_FAILED) {
        error_report("Can't map notify region.");
        return -1;
    }

    vfio->notify[qid].mmap.addr = addr;
    vfio->notify[qid].mmap.size = size;

    /* The notify_offset of each queue is queue_select */
    paddr = proxy->modern_bar.addr + proxy->notify.offset +
                virtio_pci_queue_mem_mult(proxy) * qid;

    name = g_strdup_printf("vhost-user/vfio@%p mmaps[%d]", vfio, qid);
    memory_region_init_ram_device_ptr(&vfio->notify[qid].mr,
                                      memory_region_owner(sysmem),
                                      name, size, addr);
    g_free(name);
    memory_region_add_subregion(sysmem, paddr, &vfio->notify[qid].mr);

    return 0;
}

static int vhost_user_del_mapping(struct vhost_dev *dev, int qid)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_user_vfio_state *vfio = &u->shared->vfio;
    MemoryRegion *sysmem = get_system_memory();

    if (vfio->notify[qid].mmap.addr == NULL) {
        return 0;
    }

    memory_region_del_subregion(sysmem, &vfio->notify[qid].mr);
    object_unparent(OBJECT(&vfio->notify[qid].mr));

    munmap(vfio->notify[qid].mmap.addr, vfio->notify[qid].mmap.size);
    vfio->notify[qid].mmap.addr = NULL;
    vfio->notify[qid].mmap.size = 0;

    return 0;
}

static int vhost_user_handle_vfio_set_vring_notify_area(struct vhost_dev *dev,
        VhostUserVringArea *notify_area, int fd)
{
    struct vhost_user *u = dev->opaque;
    struct vhost_user_vfio_state *vfio = &u->shared->vfio;
    int qid = notify_area->u64 & VHOST_USER_VRING_IDX_MASK;
    int nvqs, ret = 0;

    qemu_mutex_lock(&vfio->lock);

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_VFIO)) {
        ret = -1;
        goto out;
    }

    if (dev->vdev == NULL) {
        error_report("vhost_dev isn't available.");
        ret = -1;
        goto out;
    }

    nvqs = virtio_get_num_queues(dev->vdev);
    if (qid >= nvqs) {
        error_report("invalid queue index.");
        ret = -1;
        goto out;
    }

    if (vfio->notify[qid].mmap.addr != NULL) {
        vhost_user_del_mapping(dev, qid);
    }

    if (notify_area->u64 & VHOST_USER_VRING_NOFD_MASK) {
        goto out;
    }

    ret = vhost_user_add_mapping(dev, qid, fd, notify_area->size,
                                 notify_area->offset);

out:
    if (fd != -1) {
        close(fd);
    }
    qemu_mutex_unlock(&vfio->lock);
    return ret;
}

static void slave_read(void *opaque)
{
    struct vhost_dev *dev = opaque;
    struct vhost_user *u = dev->opaque;
    VhostUserMsg msg = { 0, };
    int size, ret = 0;
    struct iovec iov;
    struct msghdr msgh;
    int fd = -1;
    size_t fdsize = sizeof(fd);
    char control[CMSG_SPACE(fdsize)];
    struct cmsghdr *cmsg;

    memset(&msgh, 0, sizeof(msgh));
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    /* Read header */
    iov.iov_base = &msg;
    iov.iov_len = VHOST_USER_HDR_SIZE;

    size = recvmsg(u->slave_fd, &msgh, 0);
    if (size != VHOST_USER_HDR_SIZE) {
        error_report("Failed to read from slave.");
        goto err;
    }

    if (msgh.msg_flags & MSG_CTRUNC) {
        error_report("Truncated message.");
        goto err;
    }

    for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS) {
                    memcpy(&fd, CMSG_DATA(cmsg), fdsize);
                    break;
            }
    }

    if (msg.size > VHOST_USER_PAYLOAD_SIZE) {
        error_report("Failed to read msg header."
                " Size %d exceeds the maximum %zu.", msg.size,
                VHOST_USER_PAYLOAD_SIZE);
        goto err;
    }

    /* Read payload */
    size = read(u->slave_fd, &msg.payload, msg.size);
    if (size != msg.size) {
        error_report("Failed to read payload from slave.");
        goto err;
    }

    switch (msg.request) {
    case VHOST_USER_SLAVE_IOTLB_MSG:
        ret = vhost_backend_handle_iotlb_msg(dev, &msg.payload.iotlb);
        break;
    case VHOST_USER_SLAVE_VFIO_SET_VRING_GROUP_FD:
        ret = vhost_user_handle_vfio_set_vring_group_fd(dev,
                    msg.payload.u64, fd);
        break;
    case VHOST_USER_SLAVE_VFIO_SET_VRING_NOTIFY_AREA:
        ret = vhost_user_handle_vfio_set_vring_notify_area(dev,
                    &msg.payload.area, fd);
        break;
    default:
        error_report("Received unexpected msg type.");
        if (fd != -1) {
            close(fd);
        }
        ret = -EINVAL;
    }

    /* Message handlers need to make sure that fd will be consumed. */
    fd = -1;

    /*
     * REPLY_ACK feature handling. Other reply types has to be managed
     * directly in their request handlers.
     */
    if (msg.flags & VHOST_USER_NEED_REPLY_MASK) {
        msg.flags &= ~VHOST_USER_NEED_REPLY_MASK;
        msg.flags |= VHOST_USER_REPLY_MASK;

        msg.payload.u64 = !!ret;
        msg.size = sizeof(msg.payload.u64);

        size = write(u->slave_fd, &msg, VHOST_USER_HDR_SIZE + msg.size);
        if (size != VHOST_USER_HDR_SIZE + msg.size) {
            error_report("Failed to send msg reply to slave.");
            goto err;
        }
    }

    return;

err:
    qemu_set_fd_handler(u->slave_fd, NULL, NULL, NULL);
    close(u->slave_fd);
    u->slave_fd = -1;
    if (fd != -1) {
        close(fd);
    }
    return;
}

static int vhost_setup_slave_channel(struct vhost_dev *dev)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_SET_SLAVE_REQ_FD,
        .flags = VHOST_USER_VERSION,
    };
    struct vhost_user *u = dev->opaque;
    int sv[2], ret = 0;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);

    if (!virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_SLAVE_REQ)) {
        return 0;
    }

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        error_report("socketpair() failed");
        return -1;
    }

    u->slave_fd = sv[0];
    qemu_set_fd_handler(u->slave_fd, slave_read, NULL, dev);

    if (reply_supported) {
        msg.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    ret = vhost_user_write(dev, &msg, &sv[1], 1);
    if (ret) {
        goto out;
    }

    if (reply_supported) {
        ret = process_message_reply(dev, &msg);
    }

out:
    close(sv[1]);
    if (ret) {
        qemu_set_fd_handler(u->slave_fd, NULL, NULL, NULL);
        close(u->slave_fd);
        u->slave_fd = -1;
    }

    return ret;
}

static int vhost_user_init(struct vhost_dev *dev, void *opaque)
{
    uint64_t features, protocol_features;
    struct vhost_user *u;
    int i, err;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = g_new0(struct vhost_user, 1);
    u->shared = opaque;
    u->slave_fd = -1;
    dev->opaque = u;

    if (dev->vq_index == 0) {
        for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
            u->shared->vfio.group_id[i] = -1;
        }
        qemu_mutex_init(&u->shared->vfio.lock);
    }

    err = vhost_user_get_features(dev, &features);
    if (err < 0) {
        return err;
    }

    if (virtio_has_feature(features, VHOST_USER_F_PROTOCOL_FEATURES)) {
        dev->backend_features |= 1ULL << VHOST_USER_F_PROTOCOL_FEATURES;

        err = vhost_user_get_u64(dev, VHOST_USER_GET_PROTOCOL_FEATURES,
                                 &protocol_features);
        if (err < 0) {
            return err;
        }

        dev->protocol_features =
            protocol_features & VHOST_USER_PROTOCOL_FEATURE_MASK;
        err = vhost_user_set_protocol_features(dev, dev->protocol_features);
        if (err < 0) {
            return err;
        }

        /* query the max queues we support if backend supports Multiple Queue */
        if (dev->protocol_features & (1ULL << VHOST_USER_PROTOCOL_F_MQ)) {
            err = vhost_user_get_u64(dev, VHOST_USER_GET_QUEUE_NUM,
                                     &dev->max_queues);
            if (err < 0) {
                return err;
            }
        }

        if (virtio_has_feature(features, VIRTIO_F_IOMMU_PLATFORM) &&
                !(virtio_has_feature(dev->protocol_features,
                    VHOST_USER_PROTOCOL_F_SLAVE_REQ) &&
                 virtio_has_feature(dev->protocol_features,
                    VHOST_USER_PROTOCOL_F_REPLY_ACK))) {
            error_report("IOMMU support requires reply-ack and "
                         "slave-req protocol features.");
            return -1;
        }
    }

    if (dev->migration_blocker == NULL &&
        !virtio_has_feature(dev->protocol_features,
                            VHOST_USER_PROTOCOL_F_LOG_SHMFD)) {
        error_setg(&dev->migration_blocker,
                   "Migration disabled: vhost-user backend lacks "
                   "VHOST_USER_PROTOCOL_F_LOG_SHMFD feature.");
    }

    err = vhost_setup_slave_channel(dev);
    if (err < 0) {
        return err;
    }

    return 0;
}

static int vhost_user_cleanup(struct vhost_dev *dev)
{
    struct vhost_user *u;
    int i;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = dev->opaque;
    if (u->slave_fd >= 0) {
        qemu_set_fd_handler(u->slave_fd, NULL, NULL, NULL);
        close(u->slave_fd);
        u->slave_fd = -1;
    }

    if (dev->vq_index == 0) {
        for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
            vhost_user_del_mapping(dev, i);
        }

#ifdef CONFIG_KVM
        while (u->shared->vfio.nr_group > 0) {
            int group_id;
            group_id = u->shared->vfio.group[0].id;
            vhost_user_kvm_del_vfio_group(dev, group_id);
        }
#endif
    }

    g_free(u);
    dev->opaque = 0;

    return 0;
}

static int vhost_user_get_vq_index(struct vhost_dev *dev, int idx)
{
    assert(idx >= dev->vq_index && idx < dev->vq_index + dev->nvqs);

    return idx;
}

static int vhost_user_memslots_limit(struct vhost_dev *dev)
{
    return VHOST_MEMORY_MAX_NREGIONS;
}

static bool vhost_user_requires_shm_log(struct vhost_dev *dev)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    return virtio_has_feature(dev->protocol_features,
                              VHOST_USER_PROTOCOL_F_LOG_SHMFD);
}

static int vhost_user_migration_done(struct vhost_dev *dev, char* mac_addr)
{
    VhostUserMsg msg = { 0 };

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    /* If guest supports GUEST_ANNOUNCE do nothing */
    if (virtio_has_feature(dev->acked_features, VIRTIO_NET_F_GUEST_ANNOUNCE)) {
        return 0;
    }

    /* if backend supports VHOST_USER_PROTOCOL_F_RARP ask it to send the RARP */
    if (virtio_has_feature(dev->protocol_features,
                           VHOST_USER_PROTOCOL_F_RARP)) {
        msg.request = VHOST_USER_SEND_RARP;
        msg.flags = VHOST_USER_VERSION;
        memcpy((char *)&msg.payload.u64, mac_addr, 6);
        msg.size = sizeof(msg.payload.u64);

        return vhost_user_write(dev, &msg, NULL, 0);
    }
    return -1;
}

static bool vhost_user_can_merge(struct vhost_dev *dev,
                                 uint64_t start1, uint64_t size1,
                                 uint64_t start2, uint64_t size2)
{
    ram_addr_t offset;
    int mfd, rfd;
    MemoryRegion *mr;

    mr = memory_region_from_host((void *)(uintptr_t)start1, &offset);
    mfd = memory_region_get_fd(mr);

    mr = memory_region_from_host((void *)(uintptr_t)start2, &offset);
    rfd = memory_region_get_fd(mr);

    return mfd == rfd;
}

static int vhost_user_net_set_mtu(struct vhost_dev *dev, uint16_t mtu)
{
    VhostUserMsg msg;
    bool reply_supported = virtio_has_feature(dev->protocol_features,
                                              VHOST_USER_PROTOCOL_F_REPLY_ACK);

    if (!(dev->protocol_features & (1ULL << VHOST_USER_PROTOCOL_F_NET_MTU))) {
        return 0;
    }

    msg.request = VHOST_USER_NET_SET_MTU;
    msg.payload.u64 = mtu;
    msg.size = sizeof(msg.payload.u64);
    msg.flags = VHOST_USER_VERSION;
    if (reply_supported) {
        msg.flags |= VHOST_USER_NEED_REPLY_MASK;
    }

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    /* If reply_ack supported, slave has to ack specified MTU is valid */
    if (reply_supported) {
        return process_message_reply(dev, &msg);
    }

    return 0;
}

static int vhost_user_send_device_iotlb_msg(struct vhost_dev *dev,
                                            struct vhost_iotlb_msg *imsg)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_IOTLB_MSG,
        .size = sizeof(msg.payload.iotlb),
        .flags = VHOST_USER_VERSION | VHOST_USER_NEED_REPLY_MASK,
        .payload.iotlb = *imsg,
    };

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -EFAULT;
    }

    return process_message_reply(dev, &msg);
}


static void vhost_user_set_iotlb_callback(struct vhost_dev *dev, int enabled)
{
    /* No-op as the receive channel is not dedicated to IOTLB messages. */
}

const VhostOps user_ops = {
        .backend_type = VHOST_BACKEND_TYPE_USER,
        .vhost_backend_init = vhost_user_init,
        .vhost_backend_cleanup = vhost_user_cleanup,
        .vhost_backend_memslots_limit = vhost_user_memslots_limit,
        .vhost_set_log_base = vhost_user_set_log_base,
        .vhost_set_mem_table = vhost_user_set_mem_table,
        .vhost_set_vring_addr = vhost_user_set_vring_addr,
        .vhost_set_vring_endian = vhost_user_set_vring_endian,
        .vhost_set_vring_num = vhost_user_set_vring_num,
        .vhost_set_vring_base = vhost_user_set_vring_base,
        .vhost_get_vring_base = vhost_user_get_vring_base,
        .vhost_set_vring_kick = vhost_user_set_vring_kick,
        .vhost_set_vring_call = vhost_user_set_vring_call,
        .vhost_set_features = vhost_user_set_features,
        .vhost_get_features = vhost_user_get_features,
        .vhost_set_owner = vhost_user_set_owner,
        .vhost_reset_device = vhost_user_reset_device,
        .vhost_get_vq_index = vhost_user_get_vq_index,
        .vhost_set_vring_enable = vhost_user_set_vring_enable,
        .vhost_requires_shm_log = vhost_user_requires_shm_log,
        .vhost_migration_done = vhost_user_migration_done,
        .vhost_backend_can_merge = vhost_user_can_merge,
        .vhost_net_set_mtu = vhost_user_net_set_mtu,
        .vhost_set_iotlb_callback = vhost_user_set_iotlb_callback,
        .vhost_send_device_iotlb_msg = vhost_user_send_device_iotlb_msg,
};
