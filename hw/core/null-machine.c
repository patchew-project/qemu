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

struct NoneMachineState {
    MachineState parent;
};

#define TYPE_NONE_MACHINE MACHINE_TYPE_NAME("none")
OBJECT_DECLARE_SIMPLE_TYPE(NoneMachineState, NONE_MACHINE)

static void machine_none_init(MachineState *mch)
{
    CPUState *cpu = NULL;

    /* Initialize CPU (if user asked for it) */
    if (mch->cpu_type) {
        cpu = cpu_create(mch->cpu_type);
        if (!cpu) {
            error_report("Unable to initialize CPU");
            exit(1);
        }
    }

    /* RAM at address zero */
    if (mch->ram) {
        memory_region_add_subregion(get_system_memory(), 0, mch->ram);
    }

    if (mch->kernel_filename) {
        error_report("The -kernel parameter is not supported "
                     "(use the generic 'loader' device instead).");
        exit(1);
    }
}

static void machine_none_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "empty machine";
    mc->init = machine_none_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 0;
    mc->default_ram_id = "ram";
    mc->no_serial = 1;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
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
