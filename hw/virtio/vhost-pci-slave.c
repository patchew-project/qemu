/*
 * Vhost-pci Slave
 *
 * Copyright Intel Corp. 2017
 *
 * Authors:
 * Wei Wang    <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <qemu/osdep.h>
#include <qemu/sockets.h>

#include "monitor/qdev.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/vhost-pci-slave.h"
#include "hw/virtio/vhost-user.h"

/*
 * The basic feature bits for all vhost-pci devices. It will be or-ed
 * with a device specific features(e.g. VHOST_PCI_NET_FEATURE_BITS),
 * defined below.
 */
#define VHOST_PCI_FEATURE_BITS (1ULL << VIRTIO_F_VERSION_1)

/*
 * The device features here are sent to the remote virtio-net device for
 * a negotiation first. Then the remotely negotiated features are given
 * to the vhost-pci-net device to negotiate with its driver.
 */
#define VHOST_PCI_NET_FEATURE_BITS ((1ULL << VIRTIO_NET_F_MRG_RXBUF) | \
                                    (1ULL << VIRTIO_NET_F_CTRL_VQ) | \
                                    (1ULL << VIRTIO_NET_F_MQ))

VhostPCISlave *vp_slave;

VhostPCIDev *get_vhost_pci_dev(void)
{
    return vp_slave->vp_dev;
}

/* Clean up VhostPCIDev */
static void vp_dev_cleanup(void)
{
    int ret;
    uint32_t i, nregions;
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    Remoteq *remoteq;

    /*
     * Normally, the pointer shoud have pointed to the slave device's vdev.
     * Otherwise, it means that no vhost-pci device has been created yet.
     * In this case, just return.
     */
    if (!vp_dev->vdev) {
        return;
    }

    nregions = vp_dev->remote_mem_num;
    for (i = 0; i < nregions; i++) {
        ret = munmap(vp_dev->mr_map_base[i], vp_dev->mr_map_size[i]);
        if (ret < 0) {
            error_report("%s: failed to unmap mr[%d]", __func__, i);
            continue;
        }
        memory_region_del_subregion(vp_dev->bar_mr, vp_dev->sub_mr + i);
    }

    if (!QLIST_EMPTY(&vp_dev->remoteq_list)) {
        QLIST_FOREACH(remoteq, &vp_dev->remoteq_list, node)
            g_free(remoteq);
    }
    QLIST_INIT(&vp_dev->remoteq_list);
    vp_dev->remoteq_num = 0;
    vp_dev->vdev = NULL;
}

static int vp_slave_write(CharBackend *chr_be, VhostUserMsg *msg)
{
    int size;

    if (!msg) {
        return 0;
    }

    /* The payload size has been assigned, plus the header size here */
    size = msg->size + VHOST_USER_HDR_SIZE;
    msg->flags &= ~VHOST_USER_VERSION_MASK;
    msg->flags |= VHOST_USER_VERSION;

    return qemu_chr_fe_write_all(chr_be, (const uint8_t *)msg, size)
           == size ? 0 : -1;
}

static int vp_slave_get_features(CharBackend *chr_be, VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;

    /* Offer the initial features, which have the protocol feature bit set */
    msg->payload.u64 = vp_dev->feature_bits;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

static void vp_slave_set_features(VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;

    /*
     * Get the remotely negotiated feature bits. They will be later taken by
     * the vhost-pci device to negotiate with its driver. Clear the protocol
     * feature bit, which is useless for the device and driver negotiation.
     */
    vp_dev->feature_bits = msg->payload.u64 &
                           ~(1 << VHOST_USER_F_PROTOCOL_FEATURES);
}

static int vp_slave_send_u64(int request, uint64_t u64)
{
    VhostUserMsg msg = {
        .request = request,
        .flags = VHOST_USER_VERSION,
        .payload.u64 = u64,
        .size = sizeof(msg.payload.u64),
    };

    if (vp_slave_write(&vp_slave->chr_be, &msg) < 0) {
        error_report("%s: failed to send", __func__);
        return -1;
    }

    return 0;
}

int vp_slave_send_feature_bits(uint64_t features)
{
    return vp_slave_send_u64(VHOST_USER_SET_FEATURES, features);
}

static void vp_slave_event(void *opaque, int event)
{
    switch (event) {
    case CHR_EVENT_OPENED:
        break;
    case CHR_EVENT_CLOSED:
        break;
    }
}

static int vp_slave_get_protocol_features(CharBackend *chr_be,
                                          VhostUserMsg *msg)
{
    msg->payload.u64 = VHOST_USER_PROTOCOL_FEATURES;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

static void vp_slave_set_device_type(VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    vp_dev->dev_type = (uint16_t)msg->payload.u64;

    switch (vp_dev->dev_type) {
    case VIRTIO_ID_NET:
        vp_dev->feature_bits |= VHOST_PCI_FEATURE_BITS |
                                VHOST_PCI_NET_FEATURE_BITS;
        break;
    default:
        error_report("%s: device type %d is not supported",
                     __func__, vp_dev->dev_type);
    }
}

static int vp_slave_get_queue_num(CharBackend *chr_be, VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;

    switch (vp_dev->dev_type) {
    case VIRTIO_ID_NET:
        msg->payload.u64 = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX;
        break;
    default:
        error_report("%s: device type %d is not supported", __func__,
                     vp_dev->dev_type);
        return -1;
    }
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

/* Calculate the memory size of all the regions */
static uint64_t vp_slave_peer_mem_size_get(VhostUserMemory *mem)
{
    int i;
    uint64_t total_size = 0;
    uint32_t nregions = mem->nregions;
    VhostUserMemoryRegion *mem_regions = mem->regions;

    for (i = 0; i < nregions; i++) {
        total_size += mem_regions[i].memory_size;
    }

    return total_size;
}

/* Prepare the memory for the vhost-pci device bar */
static int vp_slave_set_mem_table(VhostUserMsg *msg, int *fds, int fd_num)
{
    VhostUserMemory *mem = &msg->payload.memory;
    VhostUserMemoryRegion *mem_region = mem->regions;
    uint32_t i, nregions = mem->nregions;
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    vp_dev->remote_mem_num = nregions;
    MemoryRegion *bar_mr, *sub_mr;
    uint64_t bar_size, bar_map_offset = 0;
    RemoteMem *rmem;
    void *mr_qva;

    /* Sanity Check */
    if (fd_num != nregions) {
        error_report("%s: fd num doesn't match region num", __func__);
        return -1;
    }

    if (!vp_dev->bar_mr) {
        vp_dev->bar_mr = g_malloc(sizeof(MemoryRegion));
    }
    if (!vp_dev->sub_mr) {
        vp_dev->sub_mr = g_malloc(nregions * sizeof(MemoryRegion));
    }
    bar_mr = vp_dev->bar_mr;
    sub_mr = vp_dev->sub_mr;

    bar_size = vp_slave_peer_mem_size_get(mem);
    bar_size = pow2ceil(bar_size);
    memory_region_init(bar_mr, NULL, "RemoteMemory", bar_size);
    for (i = 0; i < nregions; i++) {
        vp_dev->mr_map_size[i] = mem_region[i].memory_size +
                                 mem_region[i].mmap_offset;
        /*
         * Map the remote memory by QEMU. They will then be exposed to the
         * guest via a vhost-pci device BAR. The mapped base addr and size
         * are recorded for cleanup() to use.
         */
        vp_dev->mr_map_base[i] = mmap(NULL, vp_dev->mr_map_size[i],
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      fds[i], 0);
        if (vp_dev->mr_map_base[i] == MAP_FAILED) {
            error_report("%s: map peer memory region %d failed", __func__, i);
            return -1;
        }

        mr_qva = vp_dev->mr_map_base[i] + mem_region[i].mmap_offset;
        /*
         * The BAR MMIO is different from the traditional one, because the
         * memory is set up as a regular RAM. Guest will be able to directly
         * access it, just like accessing its RAM memory.
         */
        memory_region_init_ram_ptr(&sub_mr[i], NULL, "RemoteMemory",
                                   mem_region[i].memory_size, mr_qva);
        /*
         * The remote memory regions, which are scattered in the remote VM's
         * address space, are put continuous in the BAR.
         */
        memory_region_add_subregion(bar_mr, bar_map_offset, &sub_mr[i]);
        bar_map_offset += mem_region[i].memory_size;
        rmem = &vp_dev->remote_mem[i];
        rmem->gpa = mem_region[i].guest_phys_addr;
        rmem->size = mem_region[i].memory_size;
    }
    vp_dev->bar_map_offset = bar_map_offset;

    return 0;
}

static void vp_slave_alloc_remoteq(void)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;

    Remoteq *remoteq = g_malloc0(sizeof(Remoteq));
    /*
     * Put the new allocated remoteq to the list, because we don't know how
     * many remoteq the remote device will send to us. So, when they sent one,
     * insert it to the list.
     */
    QLIST_INSERT_HEAD(&vp_dev->remoteq_list, remoteq, node);
    vp_dev->remoteq_num++;
}

static void vp_slave_set_vring_num(VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    /*
     * The info (vring_num, base etc) is sent for last remoteq, which was put
     * on the first of the list and have not been filled with those info.
     */
    Remoteq *remoteq = QLIST_FIRST(&vp_dev->remoteq_list);

    remoteq->vring_num = msg->payload.u64;
}

static void vp_slave_set_vring_base(VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    Remoteq *remoteq = QLIST_FIRST(&vp_dev->remoteq_list);

    remoteq->last_avail_idx = msg->payload.u64;
}

static int vp_slave_get_vring_base(CharBackend *chr_be, VhostUserMsg *msg)
{
    msg->flags |= VHOST_USER_REPLY_MASK;
    msg->size = sizeof(m.payload.state);
    /* Send back the last_avail_idx, which is 0 here */
    msg->payload.state.num = 0;

    return vp_slave_write(chr_be, msg);
}

static void vp_slave_set_vring_addr(VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    Remoteq *remoteq = QLIST_FIRST(&vp_dev->remoteq_list);
    memcpy(&remoteq->addr, &msg->payload.addr,
           sizeof(struct vhost_vring_addr));
}

static void vp_slave_set_vring_kick(int fd)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    Remoteq *remoteq = QLIST_FIRST(&vp_dev->remoteq_list);
    if (remoteq) {
        remoteq->kickfd = fd;
    }
}

static void vp_slave_set_vring_call(int fd)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    Remoteq *remoteq = QLIST_FIRST(&vp_dev->remoteq_list);
    if (remoteq) {
        remoteq->callfd = fd;
    }
}

static void vp_slave_set_vring_enable(VhostUserMsg *msg)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;
    struct vhost_vring_state *state = &msg->payload.state;
    Remoteq *remoteq;
    QLIST_FOREACH(remoteq, &vp_dev->remoteq_list, node) {
        if (remoteq->vring_num == state->index) {
            remoteq->enabled = (int)state->num;
            break;
        }
    }
}

static int vp_slave_device_create(uint16_t virtio_id)
{
    Error *local_err = NULL;
    QemuOpts *opts;
    DeviceState *dev;
    char params[50];

    switch (virtio_id) {
    case VIRTIO_ID_NET:
        strcpy(params, "driver=vhost-pci-net-pci,id=vhost-pci-0");
        break;
    default:
        error_report("%s: device type %d not supported", __func__, virtio_id);
    }

    opts = qemu_opts_parse_noisily(qemu_find_opts("device"), params, true);
    dev = qdev_device_add(opts, &local_err);
    if (!dev) {
        qemu_opts_del(opts);
        return -1;
    }
    object_unref(OBJECT(dev));
    return 0;
}

static int vp_slave_set_vhost_pci(VhostUserMsg *msg)
{
    int ret = 0;
    uint8_t cmd = (uint8_t)msg->payload.u64;
    VhostPCIDev *vp_dev = vp_slave->vp_dev;

    switch (cmd) {
    case VHOST_USER_SET_VHOST_PCI_START:
        ret = vp_slave_device_create(vp_dev->dev_type);
        if (ret < 0) {
            return ret;
        }
        break;
    case VHOST_USER_SET_VHOST_PCI_STOP:
        break;
    default:
        error_report("%s: cmd %d not supported", __func__, cmd);
        return -1;
    }

    return ret;
}

static int vp_slave_can_read(void *opaque)
{
    return VHOST_USER_HDR_SIZE;
}

static void vp_slave_read(void *opaque, const uint8_t *buf, int size)
{
    int ret, fd_num, fds[MAX_GUEST_REGION];
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    CharBackend *chr_be = (CharBackend *)opaque;

    if (size != VHOST_USER_HDR_SIZE) {
        error_report("%s: wrong message size received %d", __func__, size);
        return;
    }

    memcpy(p, buf, VHOST_USER_HDR_SIZE);

    if (msg.size) {
        p += VHOST_USER_HDR_SIZE;
        size = qemu_chr_fe_read_all(chr_be, p, msg.size);
        if (size != msg.size) {
            error_report("%s: wrong message size received %d != %d", __func__,
                         size, msg.size);
            return;
        }
    }

    if (msg.request > VHOST_USER_MAX) {
        error_report("%s: read an incorrect msg %d", __func__, msg.request);
        return;
    }

    switch (msg.request) {
    case VHOST_USER_GET_FEATURES:
        ret = vp_slave_get_features(chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_FEATURES:
        vp_slave_set_features(&msg);
        break;
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        ret = vp_slave_get_protocol_features(chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_PROTOCOL_FEATURES:
        break;
    case VHOST_USER_SET_DEVICE_ID:
        /*
         * Now, we know the remote device type. Make the related device feature
         * bits ready. The remote device will ask for it soon.
         */
        vp_slave_set_device_type(&msg);
        break;
    case VHOST_USER_GET_QUEUE_NUM:
        ret = vp_slave_get_queue_num(chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_OWNER:
        break;
    case VHOST_USER_SET_MEM_TABLE:
        /*
         * Currently, we don't support adding more memory to the vhost-pci
         * device after the device is realized in QEMU. So, just "break" here
         * in this case.
         */
        if (vp_slave->vp_dev->vdev) {
                break;
        }
        fd_num = qemu_chr_fe_get_msgfds(chr_be, fds, sizeof(fds) / sizeof(int));
        vp_slave_set_mem_table(&msg, fds, fd_num);
        break;
    case VHOST_USER_SET_VRING_NUM:
       /*
        * This is the first message about a remoteq. Other messages (e.g. BASE,
        * ADDR, KICK etc) will follow this message and come soon. So, allocate
        * a Rqmotq structure here, and ready to record info about the remoteq
        * from the upcoming messages.
        */
        vp_slave_alloc_remoteq();
        vp_slave_set_vring_num(&msg);
        break;
    case VHOST_USER_SET_VRING_BASE:
        vp_slave_set_vring_base(&msg);
        break;
    case VHOST_USER_GET_VRING_BASE:
        ret = vp_slave_get_vring_base(chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_VRING_ADDR:
        vp_slave_set_vring_addr(&msg);
        break;
    case VHOST_USER_SET_VRING_KICK:
        /* Consume the fd */
        qemu_chr_fe_get_msgfds(chr_be, fds, 1);
        vp_slave_set_vring_kick(fds[0]);
        /*
         * This is a non-blocking eventfd.
         * The receive function forces it to be blocking,
         * so revert it back to non-blocking.
         */
        qemu_set_nonblock(fds[0]);
        break;
    case VHOST_USER_SET_VRING_CALL:
        /* Consume the fd */
        qemu_chr_fe_get_msgfds(chr_be, fds, 1);
        vp_slave_set_vring_call(fds[0]);
        /*
         * This is a non-blocking eventfd.
         * The receive function forces it to be blocking,
         * so revert it back to non-blocking.
         */
        qemu_set_nonblock(fds[0]);
        break;
    case VHOST_USER_SET_VRING_ENABLE:
        vp_slave_set_vring_enable(&msg);
        break;
    case VHOST_USER_SET_LOG_BASE:
        break;
    case VHOST_USER_SET_LOG_FD:
        qemu_chr_fe_get_msgfds(chr_be, fds, 1);
        close(fds[0]);
        break;
    case VHOST_USER_SEND_RARP:
        break;
    case VHOST_USER_SET_VHOST_PCI:
        ret = vp_slave_set_vhost_pci(&msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    default:
        error_report("vhost-pci-slave does not support msg request = %d",
                     msg.request);
        break;
    }
    return;

err_handling:
    error_report("%s: handle request %d failed", __func__, msg.request);
}

static Chardev *vp_slave_parse_chardev(const char *id)
{
    Chardev *chr = qemu_chr_find(id);
    if (!chr) {
        error_report("chardev \"%s\" not found", id);
        return NULL;
    }

    return chr;
}

static void vp_dev_init(VhostPCIDev *vp_dev)
{
    vp_dev->feature_bits = 1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
    vp_dev->bar_mr = NULL;
    vp_dev->sub_mr = NULL;
    vp_dev->vdev = NULL;
    QLIST_INIT(&vp_dev->remoteq_list);
    vp_dev->remoteq_num = 0;
}

int vhost_pci_slave_init(QemuOpts *opts)
{
    Chardev *chr;
    VhostPCIDev *vp_dev;
    const char *chardev_id = qemu_opt_get(opts, "chardev");

    vp_slave = g_malloc(sizeof(VhostPCISlave));
    chr = vp_slave_parse_chardev(chardev_id);
    if (!chr) {
        return -1;
    }
    vp_dev = g_malloc(sizeof(VhostPCIDev));
    vp_dev_init(vp_dev);
    vp_slave->vp_dev = vp_dev;

    qemu_chr_fe_init(&vp_slave->chr_be, chr, &error_abort);
    qemu_chr_fe_set_handlers(&vp_slave->chr_be, vp_slave_can_read,
                             vp_slave_read, vp_slave_event,
                             &vp_slave->chr_be, NULL, true);

    return 0;
}

int vhost_pci_slave_cleanup(void)
{
    VhostPCIDev *vp_dev = vp_slave->vp_dev;

    vp_dev_cleanup();
    qemu_chr_fe_deinit(&vp_slave->chr_be);
    g_free(vp_dev->sub_mr);
    g_free(vp_dev->bar_mr);
    g_free(vp_dev);

    return 0;
}
