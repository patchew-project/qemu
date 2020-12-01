/*
 * Copyright © 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/machine.h"
#include "io/channel.h"
#include "hw/remote/mpqemu-link.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"
#include "hw/pci/pci.h"

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg);
static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg);

void coroutine_fn mpqemu_remote_msg_loop_co(void *data)
{
    RemoteCommDev *com = (RemoteCommDev *)data;
    PCIDevice *pci_dev = NULL;

    pci_dev = com->dev;
    for (;;) {
        MPQemuMsg msg = {0};
        Error *local_err = NULL;

        if (!com->ioc) {
            error_report("ERROR: No channel available");
            break;
        }
        mpqemu_msg_recv(&msg, com->ioc, &local_err);
        if (local_err) {
            error_report_err(local_err);
            break;
        }

        if (!mpqemu_msg_valid(&msg)) {
            error_report("Received invalid message from proxy"
                         "in remote process pid=%d", getpid());
            break;
        }

        switch (msg.cmd) {
        case PCI_CONFIG_WRITE:
            process_config_write(com->ioc, pci_dev, &msg);
            break;
        case PCI_CONFIG_READ:
            process_config_read(com->ioc, pci_dev, &msg);
            break;
        default:
            error_setg(&local_err,
                       "Unknown command (%d) received for device %s (pid=%d)",
                       msg.cmd, DEVICE(pci_dev)->id, getpid());
        }

        if (local_err) {
            error_report_err(local_err);
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            break;
        }
    }
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    g_free(com);

    return;
}

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg)
{
    PciConfDataMsg *conf = (PciConfDataMsg *)&msg->data.pci_conf_data;
    MPQemuMsg ret = { 0 };
    Error *local_err = NULL;

    if ((conf->addr + sizeof(conf->val)) > pci_config_size(dev)) {
        error_report("Bad address received when writing PCI config, pid %d",
                     getpid());
        ret.data.u64 = UINT64_MAX;
    } else {
        pci_default_write_config(dev, conf->addr, conf->val, conf->l);
    }

    ret.cmd = RET_MSG;
    ret.size = sizeof(ret.data.u64);

    mpqemu_msg_send(&ret, ioc, &local_err);
    if (local_err) {
        error_report("Could not send message to proxy from pid %d",
                     getpid());
    }
}

static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg)
{
    PciConfDataMsg *conf = (PciConfDataMsg *)&msg->data.pci_conf_data;
    MPQemuMsg ret = { 0 };
    Error *local_err = NULL;

    if ((conf->addr + sizeof(conf->val)) > pci_config_size(dev)) {
        error_report("Bad address received when reading PCI config, pid %d",
                     getpid());
        ret.data.u64 = UINT64_MAX;
    } else {
        ret.data.u64 = pci_default_read_config(dev, conf->addr, conf->l);
    }

    ret.cmd = RET_MSG;
    ret.size = sizeof(ret.data.u64);

    mpqemu_msg_send(&ret, ioc, &local_err);
    if (local_err) {
        error_report("Could not send message to proxy from pid %d",
                     getpid());
    }
}
