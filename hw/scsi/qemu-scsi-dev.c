/*
 * Remote SCSI device
 *
 * Copyright 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <glib.h>
#include <unistd.h>

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "remote/pcihost.h"
#include "remote/machine.h"
#include "hw/boards.h"
#include "remote/memory.h"
#include "io/proxy-link.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "qemu-common.h"

static RemMachineState *machine;
static ProxyLinkState *proxy_link;

static void process_msg(GIOCondition cond)
{
    ProcMsg *msg = NULL;
    Error *err = NULL;

    if ((cond & G_IO_HUP) || (cond & G_IO_ERR)) {
        error_setg(&err, "socket closed, cond is %d", cond);
        goto finalize_loop;
    }

    msg = g_malloc0(sizeof(ProcMsg));

    if (proxy_proc_recv(proxy_link, msg)) {
        error_setg(&err, "Failed to receive message");
        goto finalize_loop;
    }

    switch (msg->cmd) {
    case INIT:
        break;
    case CONF_WRITE:
        break;
    case CONF_READ:
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
    default:
        error_setg(&err, "Unknown command");
        goto finalize_loop;
    }

    g_free(msg);

    return;

finalize_loop:
    error_report_err(err);
    g_free(msg);
    proxy_link_finalize(proxy_link);
    proxy_link = NULL;
}

int main(int argc, char *argv[])
{
    Error *err = NULL;

    module_call_init(MODULE_INIT_QOM);

    machine = REMOTE_MACHINE(object_new(TYPE_REMOTE_MACHINE));

    current_machine = MACHINE(machine);

    if (qemu_init_main_loop(&err)) {
        error_report_err(err);
        return -EBUSY;
    }

    qemu_init_cpu_loop();

    page_size_init();

    proxy_link = proxy_link_create();
    if (!proxy_link) {
        printf("Could not create proxy link\n");
        return -1;
    }

    proxy_link_set_sock(proxy_link, STDIN_FILENO);
    proxy_link_set_callback(proxy_link, process_msg);

    start_handler(proxy_link);

    return 0;
}
