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

#include "hw/i386/remote.h"
#include "io/channel.h"
#include "io/mpqemu-link.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "hw/i386/remote-memory.h"

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg);
static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg);
static void process_bar_write(QIOChannel *ioc, MPQemuMsg *msg, Error **errp);
static void process_bar_read(QIOChannel *ioc, MPQemuMsg *msg, Error **errp);

gboolean mpqemu_process_msg(QIOChannel *ioc, GIOCondition cond,
                            gpointer opaque)
{
    PCIDevice *pci_dev = (PCIDevice *)opaque;
    Error *local_err = NULL;
    MPQemuMsg msg = { 0 };

    if (cond & G_IO_HUP) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        return FALSE;
    }

    if (cond & (G_IO_ERR | G_IO_NVAL)) {
        error_report("Error %d while processing message from proxy \
                   in remote process pid=%d", errno, getpid());
        return FALSE;
    }

    mpqemu_msg_recv(&msg, ioc, &local_err);
    if (local_err) {
        goto exit;
    }

    if (!mpqemu_msg_valid(&msg)) {
        error_report("Received invalid message from proxy \
                     in remote process pid=%d", getpid());
        return FALSE;
    }

    switch (msg.cmd) {
    case PCI_CONFIG_WRITE:
        process_config_write(ioc, pci_dev, &msg);
        break;
    case PCI_CONFIG_READ:
        process_config_read(ioc, pci_dev, &msg);
        break;
    case BAR_WRITE:
        process_bar_write(ioc, &msg, &local_err);
        break;
    case BAR_READ:
        process_bar_read(ioc, &msg, &local_err);
        break;
    case SYNC_SYSMEM:
        remote_sysmem_reconfig(&msg, &local_err);
        break;
    default:
        error_setg(&local_err,
                   "Unknown command (%d) received for device %s (pid=%d)",
                   msg.cmd, DEVICE(pci_dev)->id, getpid());
    }

    mpqemu_msg_cleanup(&msg);

exit:
    if (local_err) {
        error_report_err(local_err);
        return FALSE;
    }

    return TRUE;
}

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg)
{
    ConfDataMsg *conf = (ConfDataMsg *)msg->data2;
    MPQemuMsg ret = { 0 };
    Error *local_err = NULL;

    if ((conf->addr + sizeof(conf->val)) > pci_config_size(dev)) {
        error_report("Bad address received when writing PCI config, pid %d",
                     getpid());
        ret.data1.u64 = UINT64_MAX;
    } else {
        pci_default_write_config(dev, conf->addr, conf->val, conf->l);
    }

    ret.cmd = RET_MSG;
    ret.bytestream = 0;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc, &local_err);
    if (local_err) {
        error_report("Could not send message to proxy from pid %d",
                     getpid());
    }
}

static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg)
{
    ConfDataMsg *conf = (ConfDataMsg *)msg->data2;
    MPQemuMsg ret = { 0 };
    Error *local_err = NULL;

    if ((conf->addr + sizeof(conf->val)) > pci_config_size(dev)) {
        error_report("Bad address received when reading PCI config, pid %d",
                     getpid());
        ret.data1.u64 = UINT64_MAX;
    } else {
        ret.data1.u64 = pci_default_read_config(dev, conf->addr, conf->l);
    }

    ret.cmd = RET_MSG;
    ret.bytestream = 0;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc, &local_err);
    if (local_err) {
        error_report("Could not send message to proxy from pid %d",
                     getpid());
    }

}

static void process_bar_write(QIOChannel *ioc, MPQemuMsg *msg, Error **errp)
{
    BarAccessMsg *bar_access = &msg->data1.bar_access;
    AddressSpace *as =
        bar_access->memory ? &address_space_memory : &address_space_io;
    MPQemuMsg ret = { 0 };
    MemTxResult res;
    uint64_t val;
    Error *local_err = NULL;

    if (!is_power_of_2(bar_access->size) ||
       (bar_access->size > sizeof(uint64_t))) {
        ret.data1.u64 = UINT64_MAX;
        goto fail;
    }

    val = cpu_to_le64(bar_access->val);

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (void *)&val, bar_access->size, true);

    if (res != MEMTX_OK) {
        error_setg(errp, "Could not perform address space write operation,"
                   " inaccessible address: %lx in pid %d.",
                   bar_access->addr, getpid());
        ret.data1.u64 = -1;
    }

fail:
    ret.cmd = RET_MSG;
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc, &local_err);
    if (local_err) {
        error_setg(errp, "Error while sending message to proxy "
                   "in remote process pid=%d", getpid());
    }
}

static void process_bar_read(QIOChannel *ioc, MPQemuMsg *msg, Error **errp)
{
    BarAccessMsg *bar_access = &msg->data1.bar_access;
    MPQemuMsg ret = { 0 };
    AddressSpace *as;
    MemTxResult res;
    uint64_t val = 0;
    Error *local_err = NULL;

    as = bar_access->memory ? &address_space_memory : &address_space_io;

    if (!is_power_of_2(bar_access->size) ||
       (bar_access->size > sizeof(uint64_t))) {
        val = UINT64_MAX;
        goto fail;
    }

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (void *)&val, bar_access->size, false);

    if (res != MEMTX_OK) {
        error_setg(errp, "Could not perform address space read operation,"
                   " inaccessible address: %lx in pid %d.",
                   bar_access->addr, getpid());
        val = UINT64_MAX;
        goto fail;
    }

fail:
    ret.cmd = RET_MSG;
    ret.data1.u64 = le64_to_cpu(val);
    ret.size = sizeof(ret.data1);

    mpqemu_msg_send(&ret, ioc, &local_err);
    if (local_err) {
        error_setg(errp, "Error while sending message to proxy "
                   "in remote process pid=%d", getpid());
    }
}
