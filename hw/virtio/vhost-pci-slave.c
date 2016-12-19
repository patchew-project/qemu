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

VhostPCISlave *vp_slave;

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

    vp_slave = g_malloc(sizeof(VhostPCISlave));
    chr = vp_slave_parse_chardev(chardev_id);
    if (!chr) {
        return -1;
    }
    qemu_chr_fe_init(&vp_slave->chr_be, chr, &error_abort);

    return 0;
}

int vhost_pci_slave_cleanup(void)
{
    qemu_chr_fe_deinit(&vp_slave->chr_be);
    g_free(vp_slave);

    return 0;
}
