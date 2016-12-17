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
#include "hw/virtio/vhost-pci-slave.h"
#include "hw/virtio/vhost-user.h"

VhostPCISlave *vp_slave;

static void vp_slave_event(void *opaque, int event)
{
    switch (event) {
    case CHR_EVENT_OPENED:
        break;
    case CHR_EVENT_CLOSED:
        break;
    }
}

static int vp_slave_can_read(void *opaque)
{
    return VHOST_USER_HDR_SIZE;
}

static void vp_slave_read(void *opaque, const uint8_t *buf, int size)
{
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
    default:
        error_report("vhost-pci-slave does not support msg request = %d",
                     msg.request);
        break;
    }
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
