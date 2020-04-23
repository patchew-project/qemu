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
