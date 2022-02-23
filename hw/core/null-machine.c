/*
 * Empty machine
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "hw/core/cpu.h"
#include "qapi/visitor.h"
#include "hw/sysbus.h"

struct NoneMachineState {
    MachineState parent;
    uint64_t ram_addr;
};

#define TYPE_NONE_MACHINE MACHINE_TYPE_NAME("none")
OBJECT_DECLARE_SIMPLE_TYPE(NoneMachineState, NONE_MACHINE)

static void machine_none_init(MachineState *mch)
{
    NoneMachineState *nms = NONE_MACHINE(mch);
    CPUState *cpu = NULL;

    /* Initialize CPU (if user asked for it) */
    if (mch->cpu_type) {
        if (mch->smp.cpus > 1) {
            error_report("Cannot initialize more than 1 CPU");
            exit(1);
        }
        cpu = cpu_create(mch->cpu_type);
        if (!cpu) {
            error_report("Unable to initialize CPU");
            exit(1);
        }
    }

    /* RAM at configured address (default: 0) */
    if (mch->ram) {
        memory_region_add_subregion(get_system_memory(), nms->ram_addr,
                                    mch->ram);
    } else if (nms->ram_addr) {
        error_report("'ram-addr' has been specified but the size is zero");
        exit(1);
    }

    if (mch->kernel_filename) {
        error_report("The -kernel parameter is not supported "
                     "(use the generic 'loader' device instead).");
        exit(1);
    }
}

static void machine_none_get_ram_addr(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    NoneMachineState *nms = NONE_MACHINE(obj);

    visit_type_uint64(v, name, &nms->ram_addr, errp);
}

static void machine_none_set_ram_addr(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    NoneMachineState *nms = NONE_MACHINE(obj);

    visit_type_uint64(v, name, &nms->ram_addr, errp);
}

static void machine_none_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "empty machine";
    mc->init = machine_none_init;
    mc->max_cpus = 16;
    mc->default_ram_size = 0;
    mc->default_ram_id = "ram";
    mc->no_serial = 1;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;

    object_class_property_add(oc, "ram-addr", "int",
        machine_none_get_ram_addr,
        machine_none_set_ram_addr,
        NULL, NULL);
    object_class_property_set_description(oc, "ram-addr",
        "Base address of the RAM (default is 0)");

    /* allow cold plugging any any "user-creatable" sysbus device */
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_SYS_BUS_DEVICE);
}

static const TypeInfo none_machine_info = {
    .name          = TYPE_NONE_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(NoneMachineState),
    .class_init    = machine_none_class_init,
};

static void none_machine_register_types(void)
{
    type_register_static(&none_machine_info);
}
type_init(none_machine_register_types);
