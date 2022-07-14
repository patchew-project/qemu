/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/r52_virt.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qom/object.h"

struct r52MachineState {
    MachineState parent_obj;

    ArmR52VirtState soc;

    bool secure;
    bool virt;

    struct arm_boot_info binfo;
};

#define TYPE_R52_MACHINE   MACHINE_TYPE_NAME("r52")
OBJECT_DECLARE_SIMPLE_TYPE(r52MachineState, R52_MACHINE)


static bool r52_get_secure(Object *obj, Error **errp)
{
    r52MachineState *s = R52_MACHINE(obj);

    return s->secure;
}

static void r52_set_secure(Object *obj, bool value, Error **errp)
{
    r52MachineState *s = R52_MACHINE(obj);

    s->secure = value;
}

static bool r52_get_virt(Object *obj, Error **errp)
{
    r52MachineState *s = R52_MACHINE(obj);

    return s->virt;
}

static void r52_set_virt(Object *obj, bool value, Error **errp)
{
    r52MachineState *s = R52_MACHINE(obj);

    s->virt = value;
}

static void r52_init(MachineState *machine)
{
    r52MachineState *s = R52_MACHINE(machine);
    uint64_t ram_size = machine->ram_size;

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_ARMR52VIRT);

    object_property_set_bool(OBJECT(&s->soc), "secure", s->secure,
                             &error_fatal);
    object_property_set_bool(OBJECT(&s->soc), "virtualization", s->virt,
                             &error_fatal);

    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    s->binfo.ram_size = ram_size;
    s->binfo.loader_start = 0;
    s->binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(s->soc.boot_cpu_ptr, machine, &s->binfo);
}

static void r52_machine_instance_init(Object *obj)
{
    r52MachineState *s = R52_MACHINE(obj);

    /* Default to secure mode being disabled */
    s->secure = false;
    /* Default to virt (EL2) being enabled */
    s->virt = true;
}

static void r52_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Cortex-R52 platform";
    mc->init = r52_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->max_cpus = ARMR52_VIRT_NUM_APU_CPUS;
    mc->default_cpus = ARMR52_VIRT_NUM_APU_CPUS;

    object_class_property_add_bool(oc, "secure", r52_get_secure,
                                   r52_set_secure);
    object_class_property_set_description(oc, "secure",
                                          "Set on/off to enable/disable the ARM "
                                          "Security Extensions (TrustZone)");

    object_class_property_add_bool(oc, "virtualization", r52_get_virt,
                                   r52_set_virt);
    object_class_property_set_description(oc, "virtualization",
                                          "Set on/off to enable/disable emulating a "
                                          "guest CPU which implements the ARM "
                                          "Virtualization Extensions");
}

static const TypeInfo r52_machine_init_typeinfo = {
    .name       = TYPE_R52_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = r52_machine_class_init,
    .instance_init = r52_machine_instance_init,
    .instance_size = sizeof(r52MachineState),
};

static void r52_machine_init_register_types(void)
{
    type_register_static(&r52_machine_init_typeinfo);
}

type_init(r52_machine_init_register_types)
