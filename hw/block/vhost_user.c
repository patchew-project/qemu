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
#include "hw/hw.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio-net.h"
#include "chardev/char-fe.h"
#include "hw/block/block.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"

#include "nvme.h"
#include "vhost_user_nvme.h"

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
    VHOST_USER_NVME_ADMIN = 27,
    VHOST_USER_NVME_SET_CQ_CALL = 28,
    VHOST_USER_NVME_GET_CAP = 29,
    VHOST_USER_NVME_START_STOP = 30,
    VHOST_USER_NVME_IO_CMD = 31,
    VHOST_USER_MAX
} VhostUserRequest;

typedef enum VhostUserSlaveRequest {
    VHOST_USER_SLAVE_NONE = 0,
    VHOST_USER_SLAVE_IOTLB_MSG = 1,
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

enum VhostUserNvmeQueueTypes {
    VHOST_USER_NVME_SUBMISSION_QUEUE = 1,
    VHOST_USER_NVME_COMPLETION_QUEUE = 2,
};

typedef struct VhostUserNvmeIO {
    enum VhostUserNvmeQueueTypes queue_type;
    uint32_t qid;
    uint32_t tail_head;
} VhostUserNvmeIO;

typedef struct VhostUserMsg {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1 << 2)
#define VHOST_USER_NEED_REPLY_MASK  (0x1 << 3)
    uint32_t flags;
    uint32_t size; /* the following payload size */
    union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1 << 8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserLog log;
        struct nvme {
            union {
                NvmeCmd req;
                NvmeCqe cqe;
            } cmd;
            uint8_t buf[4096];
        } nvme;
        VhostUserNvmeIO nvme_io;
        struct vhost_iotlb_msg iotlb;
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
    CharBackend *chr;
};

static bool ioeventfd_enabled(void)
{
    return kvm_enabled() && kvm_eventfds_enabled();
}

static int vhost_user_memslots_limit(struct vhost_dev *dev)
{
    return VHOST_MEMORY_MAX_NREGIONS;
}

/* most non-init callers ignore the error */
static int vhost_user_write(struct vhost_dev *dev, VhostUserMsg *msg,
                            int *fds, int fd_num)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->chr;
    int ret, size = VHOST_USER_HDR_SIZE + msg->size;

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

static int vhost_user_read(struct vhost_dev *dev, VhostUserMsg *msg)
{
    struct vhost_user *u = dev->opaque;
    CharBackend *chr = u->chr;
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

static int vhost_user_get_u64(struct vhost_dev *dev, int request, uint64_t *u64)
{
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
    };

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

int
vhost_user_nvme_get_cap(struct vhost_dev *dev, uint64_t *cap)
{
    return vhost_user_get_u64(dev, VHOST_USER_NVME_GET_CAP, cap);
}

int vhost_dev_nvme_start(struct vhost_dev *dev, VirtIODevice *vdev)
{
    int r = 0;

    if (vdev != NULL) {
        return -1;
    }
    r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
    if (r < 0) {
        error_report("SET MEMTABLE Failed");
        return -1;
    }

    vhost_user_set_u64(dev, VHOST_USER_NVME_START_STOP, 1);

    return 0;
}

int vhost_dev_nvme_stop(struct vhost_dev *dev)
{
    return vhost_user_set_u64(dev, VHOST_USER_NVME_START_STOP, 0);
}

int
vhost_user_nvme_io_cmd_pass(struct vhost_dev *dev, uint16_t qid,
                            uint16_t tail_head, bool submission_queue)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_NVME_IO_CMD,
        .flags = VHOST_USER_VERSION,
        .size = sizeof(VhostUserNvmeIO),
    };

    if (submission_queue) {
        msg.payload.nvme_io.queue_type = VHOST_USER_NVME_SUBMISSION_QUEUE;
    } else {
        msg.payload.nvme_io.queue_type = VHOST_USER_NVME_COMPLETION_QUEUE;
    }
    msg.payload.nvme_io.qid = qid;
    msg.payload.nvme_io.tail_head = tail_head;

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    return 0;
}

/* reply required for all the messages */
int
vhost_user_nvme_admin_cmd_raw(struct vhost_dev *dev, NvmeCmd *cmd,
                              void *buf, uint32_t len)
{
    VhostUserMsg msg = {
        .request = VHOST_USER_NVME_ADMIN,
        .flags = VHOST_USER_VERSION,
    };
    uint16_t status;

    msg.size = sizeof(*cmd);
    memcpy(&msg.payload.nvme.cmd.req, cmd, sizeof(*cmd));

    if (vhost_user_write(dev, &msg, NULL, 0) < 0) {
        return -1;
    }

    if (vhost_user_read(dev, &msg) < 0) {
        return -1;
    }

    if (msg.request != VHOST_USER_NVME_ADMIN) {
        error_report("Received unexpected msg type. Expected %d received %d",
                     VHOST_USER_NVME_ADMIN, msg.request);
        return -1;
    }

    switch (cmd->opcode) {
    case NVME_ADM_CMD_DELETE_SQ:
    case NVME_ADM_CMD_CREATE_SQ:
    case NVME_ADM_CMD_DELETE_CQ:
    case NVME_ADM_CMD_CREATE_CQ:
    case NVME_ADM_CMD_DB_BUFFER_CFG:
        if (msg.size != sizeof(NvmeCqe)) {
            error_report("Received unexpected rsp message. %u received %u",
                         cmd->opcode, msg.size);
        }
        status = msg.payload.nvme.cmd.cqe.status;
        if (nvme_cpl_is_error(status)) {
            error_report("Nvme Admin Command Status Faild");
            return -1;
        }
        memcpy(buf, &msg.payload.nvme.cmd.cqe, len);
    break;
    case NVME_ADM_CMD_IDENTIFY:
    case NVME_ADM_CMD_GET_FEATURES:
    case NVME_ADM_CMD_SET_FEATURES:
        if (msg.size != sizeof(NvmeCqe) + 4096) {
            error_report("Received unexpected rsp message. %u received %u",
                         cmd->opcode, msg.size);
        }
        status = msg.payload.nvme.cmd.cqe.status;
        if (nvme_cpl_is_error(status)) {
            error_report("Nvme Admin Command Status Faild");
            return -1;
        }
        memcpy(buf, &msg.payload.nvme.buf, len);
    break;
    default:
        return -1;
    }

    return 0;
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

static int vhost_user_set_mem_table(struct vhost_dev *dev,
                                    struct vhost_memory *mem)
{
    int fds[VHOST_MEMORY_MAX_NREGIONS];
    int i, fd;
    size_t fd_num = 0;
    bool reply_supported = true;

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

static int vhost_user_set_vring_call(struct vhost_dev *dev,
                                     struct vhost_vring_file *file)
{
    return vhost_set_vring_file(dev, VHOST_USER_NVME_SET_CQ_CALL, file);
}

static int vhost_user_init(struct vhost_dev *dev, void *opaque)
{
    struct vhost_user *u;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = g_new0(struct vhost_user, 1);
    u->chr = opaque;
    dev->opaque = u;

    return 0;
}

static int vhost_user_cleanup(struct vhost_dev *dev)
{
    struct vhost_user *u;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);

    u = dev->opaque;
    g_free(u);
    dev->opaque = 0;

    return 0;
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

const VhostOps user_nvme_ops = {
        .backend_type = VHOST_BACKEND_TYPE_USER,
        .vhost_backend_init = vhost_user_init,
        .vhost_backend_cleanup = vhost_user_cleanup,
        .vhost_backend_memslots_limit = vhost_user_memslots_limit,
        .vhost_set_mem_table = vhost_user_set_mem_table,
        .vhost_set_vring_call = vhost_user_set_vring_call,
        .vhost_backend_can_merge = vhost_user_can_merge,
};

int vhost_dev_nvme_set_backend_type(struct vhost_dev *dev, VhostBackendType backend_type)
{
    int r = 0;

    switch (backend_type) {
    case VHOST_BACKEND_TYPE_USER:
        dev->vhost_ops = &user_nvme_ops;
        break;
    default:
        error_report("Unknown vhost backend type");
        r = -1;
    }

    return r;
}
