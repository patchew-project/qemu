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

static const GPtrArray *ac_compat_props;
static const GPtrArray *mc_compat_props;

void accel_register_compat_props(const GPtrArray *props)
{
    ac_compat_props = props;
}

void machine_register_compat_props(const GPtrArray *props)
{
    mc_compat_props = props;
}

static void compat_props_post_init(Object *obj)
{
    if (ac_compat_props) {
        object_apply_global_props(obj, ac_compat_props, &error_abort);
    }
    if (mc_compat_props) {
        object_apply_global_props(obj, mc_compat_props, &error_abort);
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
