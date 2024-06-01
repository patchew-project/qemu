/*
 * AWS nitro-enclave machine
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"

#include "hw/sysbus.h"
#include "hw/i386/x86.h"
#include "hw/i386/microvm.h"
#include "hw/i386/nitro_enclave.h"
#include "hw/virtio/vhost-vsock.h"
#include "hw/virtio/virtio-mmio.h"

static BusState *find_virtio_mmio_bus(void)
{
    BusChild *kid;
    BusState *bus = sysbus_get_default();

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_MMIO)) {
            VirtIOMMIOProxy *mmio = VIRTIO_MMIO(OBJECT(dev));
            VirtioBusState *mmio_virtio_bus = &mmio->bus;
            BusState *mmio_bus = &mmio_virtio_bus->parent_obj;
            return mmio_bus;
        }
    }

    return NULL;
}

static void nitro_enclave_devices_init(NitroEnclaveMachineState *nems)
{
    DeviceState *dev = qdev_new(TYPE_VHOST_VSOCK);
    BusState *bus = find_virtio_mmio_bus();
    if (!bus) {
        error_report("Failed to find bus for vhost-vsock device.");
        exit(1);
    }

    if (nems->guest_cid <= 3) {
        error_report("Nitro enclave machine option 'guest-cid' must be set "
                     "with a value greater than or equal to 4");
        exit(1);
    }

    qdev_prop_set_uint64(dev, "guest-cid", nems->guest_cid);
    qdev_realize_and_unref(dev, bus, &error_fatal);
}

static void nitro_enclave_machine_state_init(MachineState *machine)
{
    NitroEnclaveMachineClass *ne_class =
        NITRO_ENCLAVE_MACHINE_GET_CLASS(machine);
    NitroEnclaveMachineState *ne_state = NITRO_ENCLAVE_MACHINE(machine);

    ne_class->parent_init(machine);
    nitro_enclave_devices_init(ne_state);
}

static void nitro_enclave_machine_initfn(Object *obj)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    X86MachineState *x86ms = X86_MACHINE(obj);

    nems->guest_cid = 0;

    /* AWS nitro enclaves have PCIE and ACPI disabled */
    mms->pcie = ON_OFF_AUTO_OFF;
    x86ms->acpi = ON_OFF_AUTO_OFF;
}

static void nitro_enclave_get_guest_cid(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);
    uint32_t guest_cid = nems->guest_cid;

    visit_type_uint32(v, name, &guest_cid, errp);
}

static void nitro_enclave_set_guest_cid(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    visit_type_uint32(v, name, &nems->guest_cid, errp);
}

static void nitro_enclave_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    NitroEnclaveMachineClass *nemc = NITRO_ENCLAVE_MACHINE_CLASS(oc);

    mc->family = "nitro_enclave_i386";
    mc->desc = "AWS Nitro Enclave";

    nemc->parent_init = mc->init;
    mc->init = nitro_enclave_machine_state_init;

    object_class_property_add(oc, NITRO_ENCLAVE_GUEST_CID, "uint32_t",
                              nitro_enclave_get_guest_cid,
                              nitro_enclave_set_guest_cid,
                              NULL, NULL);
    object_class_property_set_description(oc, NITRO_ENCLAVE_GUEST_CID,
                                          "Set enclave machine's cid");
}

static const TypeInfo nitro_enclave_machine_info = {
    .name          = TYPE_NITRO_ENCLAVE_MACHINE,
    .parent        = TYPE_MICROVM_MACHINE,
    .instance_size = sizeof(NitroEnclaveMachineState),
    .instance_init = nitro_enclave_machine_initfn,
    .class_size    = sizeof(NitroEnclaveMachineClass),
    .class_init    = nitro_enclave_class_init,
};

static void nitro_enclave_machine_init(void)
{
    type_register_static(&nitro_enclave_machine_info);
}
type_init(nitro_enclave_machine_init);
