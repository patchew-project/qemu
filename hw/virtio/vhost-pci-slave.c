/*
 * Vhost-pci Slave
 *
 * Copyright Intel Corp. 2017
 *
 * Authors:
 * Wei Wang <wei.w.wang@intel.com>
 * Zhiyong Yang <zhiyong.yang@intel.com>
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
#include "hw/virtio/vhost-pci-net.h"

#define VHOST_USER_PROTOCOL_FEATURES (1UL << VHOST_USER_PROTOCOL_F_VHOST_PCI)

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

static int vp_slave_get_features(VhostPCINet *vpnet, CharBackend *chr_be,
                                 VhostUserMsg *msg)
{
    /* Offer the initial features, which have the protocol feature bit set */
    msg->payload.u64 = (uint64_t)vpnet->host_features |
                       (1 << VHOST_USER_F_PROTOCOL_FEATURES);
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

static void vp_slave_set_features(VhostPCINet *vpnet, VhostUserMsg *msg)
{
    vpnet->host_features = msg->payload.u64 &
                           ~(1 << VHOST_USER_F_PROTOCOL_FEATURES);
}

void vp_slave_event(void *opaque, int event)
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

static int vp_slave_get_queue_num(CharBackend *chr_be, VhostUserMsg *msg)
{
    msg->payload.u64 = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

/* Set up the vhost-pci-net device bar to map the remote memory */
static int vp_slave_set_mem_table(VhostPCINet *vpnet, VhostUserMsg *msg,
                                  int *fds, int fd_num)
{
    VhostUserMemory *mem = &msg->payload.memory;
    VhostUserMemoryRegion *region = mem->regions;
    uint32_t i, nregions = mem->nregions;
    uint64_t bar_map_offset = METADATA_SIZE;
    void *remote_mem_ptr;

    /* Sanity Check */
    if (fd_num != nregions) {
        error_report("%s: fd num doesn't match region num", __func__);
        return -1;
    }

    vpnet->metadata->nregions = nregions;
    vpnet->remote_mem_region = g_malloc(nregions * sizeof(MemoryRegion));

    for (i = 0; i < nregions; i++) {
        vpnet->remote_mem_map_size[i] = region[i].memory_size +
                                        region[i].mmap_offset;
        /*
         * Map the remote memory by QEMU. They will then be exposed to the
         * guest via a vhost-pci device BAR. The mapped base addr and size
         * are recorded and will be used when cleaning up the device.
         */
        vpnet->remote_mem_base[i] = mmap(NULL, vpnet->remote_mem_map_size[i],
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         fds[i], 0);
        if (vpnet->remote_mem_base[i] == MAP_FAILED) {
            error_report("%s: map peer memory region %d failed", __func__, i);
            return -1;
        }

        remote_mem_ptr = vpnet->remote_mem_base[i] + region[i].mmap_offset;
        /*
         * The BAR MMIO is different from the traditional one, because the
         * it is set up as a regular RAM. Guest will be able to directly
         * access it without VMExits, just like accessing its RAM memory.
         */
        memory_region_init_ram_ptr(&vpnet->remote_mem_region[i], NULL,
                                   "RemoteMemory", region[i].memory_size,
                                   remote_mem_ptr);
        /*
         * The remote memory regions, which are scattered in the remote VM's
         * address space, are put continuous in the BAR.
         */
        memory_region_add_subregion(&vpnet->bar_region, bar_map_offset,
                                    &vpnet->remote_mem_region[i]);
        bar_map_offset += region[i].memory_size;

        vpnet->metadata->mem[i].gpa = region[i].guest_phys_addr;
        vpnet->metadata->mem[i].size = region[i].memory_size;
    }

    return 0;
}

static void vp_slave_set_vring_num(VhostPCINet *vpnet, VhostUserMsg *msg)
{
    struct vhost_vring_state *state = &msg->payload.state;

    vpnet->metadata->vq[state->index].vring_num = state->num;
}

static void vp_slave_set_vring_base(VhostPCINet *vpnet, VhostUserMsg *msg)
{
    struct vhost_vring_state *state = &msg->payload.state;

    vpnet->metadata->vq[state->index].last_avail_idx = state->num;
}

static int vp_slave_get_vring_base(CharBackend *chr_be, VhostUserMsg *msg)
{
    msg->flags |= VHOST_USER_REPLY_MASK;
    msg->size = sizeof(m.payload.state);
    /* Send back the last_avail_idx, which is 0 here */
    msg->payload.state.num = 0;

    return vp_slave_write(chr_be, msg);
}

static void vp_slave_set_vring_addr(VhostPCINet *vpnet, VhostUserMsg *msg)
{
    uint32_t index = msg->payload.addr.index;

    vpnet->metadata->vq[index].desc_gpa = msg->payload.addr.desc_user_addr;
    vpnet->metadata->vq[index].avail_gpa = msg->payload.addr.avail_user_addr;
    vpnet->metadata->vq[index].used_gpa = msg->payload.addr.used_user_addr;
    vpnet->metadata->nvqs = msg->payload.addr.index + 1;
}

static void vp_slave_set_vring_enable(VhostPCINet *vpnet, VhostUserMsg *msg)
{
    struct vhost_vring_state *state = &msg->payload.state;

    vpnet->metadata->vq[state->index].vring_enabled = (int)state->num;
}

int vp_slave_can_read(void *opaque)
{
    return VHOST_USER_HDR_SIZE;
}

void vp_slave_read(void *opaque, const uint8_t *buf, int size)
{
    int ret, fd_num, fds[VHOST_MEMORY_MAX_NREGIONS];
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    VhostPCINet *vpnet = (VhostPCINet *)opaque;
    CharBackend *chr_be = &vpnet->chr_be;

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
        ret = vp_slave_get_features(vpnet, chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_FEATURES:
        vp_slave_set_features(vpnet, &msg);
        break;
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        ret = vp_slave_get_protocol_features(chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_PROTOCOL_FEATURES:
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
        fd_num = qemu_chr_fe_get_msgfds(chr_be, fds, sizeof(fds) / sizeof(int));
        vp_slave_set_mem_table(vpnet, &msg, fds, fd_num);
        break;
    case VHOST_USER_SET_VRING_NUM:
        vp_slave_set_vring_num(vpnet, &msg);
        break;
    case VHOST_USER_SET_VRING_BASE:
        vp_slave_set_vring_base(vpnet, &msg);
        break;
    case VHOST_USER_GET_VRING_BASE:
        ret = vp_slave_get_vring_base(chr_be, &msg);
        if (ret < 0) {
            goto err_handling;
        }
        break;
    case VHOST_USER_SET_VRING_ADDR:
        vp_slave_set_vring_addr(vpnet, &msg);
        break;
    case VHOST_USER_SET_VRING_KICK:
        /* Consume the fd */
        qemu_chr_fe_get_msgfds(chr_be, fds, 1);
         /*
         * This is a non-blocking eventfd.
         * The receive function forces it to be blocking,
         * so revert it back to non-blocking.
         */
        qemu_set_nonblock(fds[0]);
        break;
    case VHOST_USER_SET_VRING_CALL:
        /* Consume the fd, and revert it to non-blocking. */
        qemu_chr_fe_get_msgfds(chr_be, fds, 1);
        qemu_set_nonblock(fds[0]);
        break;
    case VHOST_USER_SET_VRING_ENABLE:
        vp_slave_set_vring_enable(vpnet, &msg);
        break;
    case VHOST_USER_SET_VHOST_PCI:
        vpnet_set_link_up(vpnet, (bool)msg.payload.u64);
        break;
    case VHOST_USER_SET_LOG_BASE:
        break;
    case VHOST_USER_SET_LOG_FD:
        qemu_chr_fe_get_msgfds(chr_be, fds, 1);
        close(fds[0]);
        break;
    case VHOST_USER_SEND_RARP:
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
