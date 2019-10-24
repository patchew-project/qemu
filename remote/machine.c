/*
 * Machine for remote device
 *
 * Copyright 2019, Oracle and/or its affiliates.
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

#include <stdint.h>
#include <sys/types.h>

#include "qemu/osdep.h"
#include "remote/pcihost.h"
#include "remote/machine.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ioport.h"
#include "exec/ramlist.h"
#include "qemu/thread.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "qemu/notify.h"

static NotifierList machine_init_done_notifiers =
    NOTIFIER_LIST_INITIALIZER(machine_init_done_notifiers);

bool machine_init_done;

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
    notifier_list_add(&machine_init_done_notifiers, notify);
    if (machine_init_done) {
        notify->notify(notify, NULL);
    }
}

void qemu_remove_machine_init_done_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

void qemu_run_machine_init_done_notifiers(void)
{
    machine_init_done = true;
    notifier_list_notify(&machine_init_done_notifiers, NULL);
}

static void remote_machine_init(Object *obj)
{
    RemMachineState *s = REMOTE_MACHINE(obj);
    RemPCIHost *rem_host;
    MemoryRegion *system_memory, *system_io, *pci_memory;

    Error *error_abort = NULL;

    qemu_mutex_init(&ram_list.mutex);

    object_property_add_child(object_get_root(), "machine", obj, &error_abort);
    if (error_abort) {
        error_report_err(error_abort);
    }

    memory_map_init();

    system_memory = get_system_memory();
    system_io = get_system_io();

    pci_memory = g_new(MemoryRegion, 1);
    memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);

    rem_host = REMOTE_HOST_DEVICE(qdev_create(NULL, TYPE_REMOTE_HOST_DEVICE));

    rem_host->mr_pci_mem = pci_memory;
    rem_host->mr_sys_mem = system_memory;
    rem_host->mr_sys_io = system_io;

    s->host = rem_host;

    qemu_mutex_lock_iothread();
    memory_region_add_subregion_overlap(system_memory, 0x0, pci_memory, -1);
    qemu_mutex_unlock_iothread();

    qdev_init_nofail(DEVICE(rem_host));
}

static const TypeInfo remote_machine = {
    .name = TYPE_REMOTE_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RemMachineState),
    .instance_init = remote_machine_init,
};

static void remote_machine_register_types(void)
{
    type_register_static(&remote_machine);
}

type_init(remote_machine_register_types);
