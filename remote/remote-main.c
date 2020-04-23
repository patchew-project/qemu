/*
 * Remote device initialization
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include <stdio.h>
#include <unistd.h>

#include "qemu/module.h"
#include "remote/pcihost.h"
#include "remote/machine.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"
#include "qemu/main-loop.h"
#include "remote/memory.h"
#include "io/mpqemu-link.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "qemu-common.h"
#include "hw/pci/pci.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "block/block.h"
#include "exec/ramlist.h"
#include "remote/remote-common.h"
#include "exec/memattrs.h"
#include "exec/address-spaces.h"
#include "remote/iohub.h"

static void process_msg(GIOCondition cond, MPQemuLinkState *link,
                        MPQemuChannel *chan);

static MPQemuLinkState *mpqemu_link;

gchar *print_pid_exec(gchar *str)
{
    if (!str) {
        return NULL;
    }
    memset(str, 0, PROC_INFO_LENGTH);
    g_snprintf(str, PROC_INFO_LENGTH, "pid %d, exec name %s ",
                                       getpid(), __progname);
    return str;
}

#define LINK_TO_DEV(link) ((PCIDevice *)link->opaque)

static void process_config_write(PCIDevice *dev, MPQemuMsg *msg)
{
    struct conf_data_msg *conf = (struct conf_data_msg *)msg->data2;

    qemu_mutex_lock_iothread();
    pci_default_write_config(dev, conf->addr, conf->val, conf->l);
    qemu_mutex_unlock_iothread();
}

static void process_config_read(PCIDevice *dev, MPQemuMsg *msg)
{
    struct conf_data_msg *conf = (struct conf_data_msg *)msg->data2;
    uint32_t val;
    int wait;

    wait = msg->fds[0];

    qemu_mutex_lock_iothread();
    val = pci_default_read_config(dev, conf->addr, conf->l);
    qemu_mutex_unlock_iothread();

    notify_proxy(wait, val);

    PUT_REMOTE_WAIT(wait);
}

static gpointer dev_thread(gpointer data)
{
    MPQemuLinkState *link = data;

    mpqemu_start_coms(link, link->dev);

    return NULL;
}

static void process_connect_dev_msg(MPQemuMsg *msg)
{
    char *devid = (char *)msg->data2;
    MPQemuLinkState *link = NULL;
    DeviceState *dev = NULL;
    int wait = msg->fds[0];
    int ret = 0;

    dev = qdev_find_recursive(sysbus_get_default(), devid);
    if (!dev) {
        ret = 0xff;
        goto exit;
    }

    link = mpqemu_link_create();
    link->opaque = (void *)PCI_DEVICE(dev);

    mpqemu_init_channel(link, &link->dev, msg->fds[1]);
    mpqemu_link_set_callback(link, process_msg);
    qemu_thread_create(&link->thread, "dev_thread", dev_thread, link,
                       QEMU_THREAD_JOINABLE);

exit:
    notify_proxy(wait, ret);
}

/* TODO: confirm memtx attrs. */
static void process_bar_write(MPQemuMsg *msg, Error **errp)
{
    bar_access_msg_t *bar_access = &msg->data1.bar_access;
    AddressSpace *as =
        bar_access->memory ? &address_space_memory : &address_space_io;
    MemTxResult res;

    assert(is_power_of_2(bar_access->size) &&
           bar_access->size <= sizeof(uint64_t));

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (uint8_t *)&bar_access->val, bar_access->size,
                           true);

    if (res != MEMTX_OK) {
        error_setg(errp, "Could not perform address space write operation,"
                   " inaccessible address: %lx in pid %d, %s.",
                   bar_access->addr, getpid(), __progname);
    }
}

static void process_bar_read(MPQemuMsg *msg, Error **errp)
{
    bar_access_msg_t *bar_access = &msg->data1.bar_access;
    AddressSpace *as;
    int wait = msg->fds[0];
    MemTxResult res;
    uint64_t val = 0;

    as = bar_access->memory ? &address_space_memory : &address_space_io;

    assert(is_power_of_2(bar_access->size) &&
           bar_access->size <= sizeof(uint64_t));

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (uint8_t *)&val, bar_access->size, false);

    if (res != MEMTX_OK) {
        error_setg(errp, "Could not perform address space read operation,"
                   " inaccessible address: %lx in pid %d, %s.",
                   bar_access->addr, getpid(), __progname);
        val = (uint64_t)-1;
        goto fail;
    }

    switch (bar_access->size) {
    case 8:
        /* Nothing to do as val is already 8 bytes long */
        break;
    case 4:
        val = *((uint32_t *)&val);
        break;
    case 2:
        val = *((uint16_t *)&val);
        break;
    case 1:
        val = *((uint8_t *)&val);
        break;
    default:
        error_setg(errp, "Invalid PCI BAR read size in pid %d, %s",
                   getpid(), __progname);
        return;
    }

fail:
    notify_proxy(wait, val);

    PUT_REMOTE_WAIT(wait);
}

static void process_msg(GIOCondition cond, MPQemuLinkState *link,
                        MPQemuChannel *chan)
{
    MPQemuMsg *msg = NULL;
    Error *err = NULL;
    g_autofree gchar *pid_exec = NULL;

    pid_exec = g_malloc(PROC_INFO_LENGTH);

    if ((cond & G_IO_HUP) || (cond & G_IO_ERR)) {
        goto finalize_loop;
    }

    msg = g_malloc0(sizeof(MPQemuMsg));

    if (mpqemu_msg_recv(msg, chan) < 0) {
        error_setg(&err, "Failed to receive message in remote process %s",
                   print_pid_exec(pid_exec));
        goto finalize_loop;
    }

    switch (msg->cmd) {
    case INIT:
        break;
    case CONNECT_DEV:
        process_connect_dev_msg(msg);
        break;
    case PCI_CONFIG_WRITE:
        process_config_write(LINK_TO_DEV(link), msg);
        break;
    case PCI_CONFIG_READ:
        process_config_read(LINK_TO_DEV(link), msg);
        break;
    case BAR_WRITE:
        process_bar_write(msg, &err);
        if (err) {
            goto finalize_loop;
        }
        break;
    case BAR_READ:
        process_bar_read(msg, &err);
        if (err) {
            goto finalize_loop;
        }
        break;
    case SYNC_SYSMEM:
        /*
         * TODO: ensure no active DMA is happening when
         * sysmem is being updated
         */
        remote_sysmem_reconfig(msg, &err);
        if (err) {
            goto finalize_loop;
        }
        break;
    case SET_IRQFD:
        process_set_irqfd_msg(LINK_TO_DEV(link), msg);
        break;
    default:
        error_setg(&err, "Unknown command in %s", print_pid_exec(pid_exec));
        goto finalize_loop;
    }

    g_free(msg->data2);
    g_free(msg);

    return;

finalize_loop:
    if (err) {
        error_report_err(err);
    }
    g_free(msg);
    mpqemu_link_finalize(mpqemu_link);
    mpqemu_link = NULL;
}

int main(int argc, char *argv[])
{
    Error *err = NULL;

    module_call_init(MODULE_INIT_QOM);

    bdrv_init_with_whitelist();

    if (qemu_init_main_loop(&err)) {
        error_report_err(err);
        return -EBUSY;
    }

    qemu_init_cpu_loop();

    page_size_init();

    qemu_mutex_init(&ram_list.mutex);

    current_machine = MACHINE(REMOTE_MACHINE(object_new(TYPE_REMOTE_MACHINE)));

    mpqemu_link = mpqemu_link_create();
    if (!mpqemu_link) {
        printf("Could not create MPQemu link pid %d, exec_name %s",
               getpid(), __progname);
        return -1;
    }

    mpqemu_init_channel(mpqemu_link, &mpqemu_link->com, STDIN_FILENO);

    mpqemu_link_set_callback(mpqemu_link, process_msg);

    qdev_machine_creation_done();
    qemu_mutex_lock_iothread();
    qemu_run_machine_init_done_notifiers();
    qemu_mutex_unlock_iothread();

    mpqemu_start_coms(mpqemu_link, mpqemu_link->com);

    return 0;
}
