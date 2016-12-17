/*
 * Vhost-pci Slave
 *
 * Copyright Intel Corp. 2016
 *
 * Authors:
 * Wei Wang    <wei.w.wang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <qemu/osdep.h>

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/vhost-pci-slave.h"
#include "hw/virtio/vhost-user.h"

#define VHOST_PCI_FEATURE_BITS (1ULL << VIRTIO_F_VERSION_1)

#define VHOST_PCI_NET_FEATURE_BITS (1ULL << VIRTIO_NET_F_MRG_RXBUF) | \
                                   (1ULL << VIRTIO_NET_F_CTRL_VQ) | \
                                   (1ULL << VIRTIO_NET_F_MQ)

VhostPCISlave *vp_slave;

static int vp_slave_write(CharBackend *chr_be, VhostUserMsg *msg)
{
    int size;

    if (!msg)
        return 0;

    size = msg->size + VHOST_USER_HDR_SIZE;
    msg->flags &= ~VHOST_USER_VERSION_MASK;
    msg->flags |= VHOST_USER_VERSION;

    return qemu_chr_fe_write_all(chr_be, (const uint8_t *)msg, size)
           == size ? 0 : -1;
}

static int vp_slave_get_features(CharBackend *chr_be, VhostUserMsg *msg)
{
    msg->payload.u64 = vp_slave->feature_bits;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

static void vp_slave_set_features(VhostUserMsg *msg)
{
   /* Clear the protocol feature bit, which is useless for the device */
    vp_slave->feature_bits = msg->payload.u64
                             & ~(1 << VHOST_USER_F_PROTOCOL_FEATURES);
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

static int vp_slave_get_protocol_features(CharBackend *chr_be, VhostUserMsg *msg)
{
    msg->payload.u64 = VHOST_USER_PROTOCOL_FEATURES;
    msg->size = sizeof(msg->payload.u64);
    msg->flags |= VHOST_USER_REPLY_MASK;

    return vp_slave_write(chr_be, msg);
}

static int vp_slave_can_read(void *opaque)
{
    return VHOST_USER_HDR_SIZE;
}

static void vp_slave_read(void *opaque, const uint8_t *buf, int size)
{
    int ret;
    VhostUserMsg msg;
    uint8_t *p = (uint8_t *) &msg;
    CharBackend *chr_be = (CharBackend *)opaque;

    if (size != VHOST_USER_HDR_SIZE) {
        error_report("Wrong message size received %d", size);
        return;
    }

    memcpy(p, buf, VHOST_USER_HDR_SIZE);

    if (msg.size) {
        p += VHOST_USER_HDR_SIZE;
        size = qemu_chr_fe_read_all(chr_be, p, msg.size);
        if (size != msg.size) {
            error_report("Wrong message size received %d != %d",
                           size, msg.size);
            return;
        }
    }

    if (msg.request > VHOST_USER_MAX)
        error_report("vhost-pci-slave read incorrect msg");

    switch(msg.request) {
    case VHOST_USER_GET_FEATURES:
        ret = vp_slave_get_features(chr_be, &msg);
        if (ret < 0)
            goto err_handling;
        break;
    case VHOST_USER_SET_FEATURES:
        vp_slave_set_features(&msg);
        break;
    case VHOST_USER_GET_PROTOCOL_FEATURES:
        ret = vp_slave_get_protocol_features(chr_be, &msg);
        if (ret < 0)
            goto err_handling;
        break;
    case VHOST_USER_SET_PROTOCOL_FEATURES:
        break;
    default:
        error_report("vhost-pci-slave does not support msg request = %d",
                     msg.request);
        break;
    }
    return;

err_handling:
    error_report("vhost-pci-slave handle request %d failed", msg.request);
}

static CharDriverState *vp_slave_parse_chardev(const char *id)
{
    CharDriverState *chr = qemu_chr_find(id);
    if (chr == NULL) {
        error_report("chardev \"%s\" not found", id);
        return NULL;
    }

    return chr;
}

int vhost_pci_slave_init(QemuOpts *opts)
{
    CharDriverState *chr;
    const char *chardev_id = qemu_opt_get(opts, "chardev");

    vp_slave = (VhostPCISlave *)g_malloc(sizeof(VhostPCISlave));
    chr = vp_slave_parse_chardev(chardev_id);
    if (!chr) {
        return -1;
    }
    vp_slave->feature_bits =  1ULL << VHOST_USER_F_PROTOCOL_FEATURES;
    qemu_chr_fe_init(&vp_slave->chr_be, chr, &error_abort);
    qemu_chr_fe_set_handlers(&vp_slave->chr_be, vp_slave_can_read,
                             vp_slave_read, vp_slave_event,
                             &vp_slave->chr_be, NULL, true);

    return 0;
}

int vhost_pci_slave_cleanup(void)
{
    qemu_chr_fe_deinit(&vp_slave->chr_be);
    g_free(vp_slave);

    return 0;
}
