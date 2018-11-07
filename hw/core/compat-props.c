/*
 * QEMU Machine compat properties
 *
 * Copyright (C) 2018 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qapi/error.h"

typedef struct CompatProps CompatProps;

typedef struct CompatPropsClass {
    InterfaceClass parent_class;
} CompatPropsClass;

static void compat_props_post_init(Object *obj)
{
    if (current_machine) {
        MachineClass *mc = MACHINE_GET_CLASS(current_machine);
        AccelClass *ac = ACCEL_GET_CLASS(current_machine->accelerator);

        object_apply_global_props(obj, mc->compat_props, &error_abort);
        object_apply_global_props(obj, ac->compat_props, &error_abort);
    }
}

static void compat_props_register_types(void)
{
    static const TypeInfo cp_interface_info = {
        .name          = TYPE_COMPAT_PROPS,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(CompatPropsClass),
        .instance_post_init = compat_props_post_init,
    };

    type_register_static(&cp_interface_info);
}

type_init(compat_props_register_types)
