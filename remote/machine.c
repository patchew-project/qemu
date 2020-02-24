/*
 * Machine for remote device
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdint.h>
#include <sys/types.h>

#include "qemu/osdep.h"
#include "remote/pcihost.h"
#include "remote/machine.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ioport.h"
#include "qemu/thread.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "qemu/notify.h"
#include "hw/pci/pci_host.h"
#include "remote/iohub.h"

static void remote_machine_init(Object *obj)
{
    RemMachineState *s = REMOTE_MACHINE(obj);
    RemPCIHost *rem_host;
    MemoryRegion *system_memory, *system_io, *pci_memory;
    PCIHostState *pci_host;
    PCIDevice *pci_dev;

    Error *error_abort = NULL;

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

    object_property_add_child(OBJECT(s), "remote-device", OBJECT(rem_host),
                              &error_abort);
    if (error_abort) {
        error_report_err(error_abort);
        return;
    }

    qemu_mutex_lock_iothread();
    memory_region_add_subregion_overlap(system_memory, 0x0, pci_memory, -1);
    qemu_mutex_unlock_iothread();

    qdev_init_nofail(DEVICE(rem_host));

    pci_host = PCI_HOST_BRIDGE(rem_host);
    pci_dev = pci_create_simple_multifunction(pci_host->bus,
                                              PCI_DEVFN(REMOTE_IOHUB_DEV,
                                                        REMOTE_IOHUB_FUNC),
                                              true, TYPE_REMOTE_IOHUB_DEVICE);

    s->iohub = REMOTE_IOHUB_DEVICE(pci_dev);

    pci_bus_irqs(pci_host->bus, remote_iohub_set_irq, remote_iohub_map_irq,
                 s->iohub, REMOTE_IOHUB_NB_PIRQS);
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
