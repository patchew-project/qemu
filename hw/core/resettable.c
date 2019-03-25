/*
 * Resettable interface.
 *
 * Copyright (c) 2019 GreenSocs SAS
 *
 * Authors:
 *   Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/resettable.h"

#define RESETTABLE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ResettableClass, (obj), TYPE_RESETTABLE)

void resettable_init_phase(Object *obj, bool cold)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);

    if (rc->phases.init) {
        rc->phases.init(obj, cold);
    }
}

void resettable_hold_phase(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);

    if (rc->phases.hold) {
        rc->phases.hold(obj);
    }
}

void resettable_exit_phase(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);

    if (rc->phases.exit) {
        rc->phases.exit(obj);
    }
}

void resettable_assert_reset(Object *obj, bool cold)
{
    resettable_init_phase(obj, cold);
    resettable_hold_phase(obj);
}

void resettable_deassert_reset(Object *obj)
{
    resettable_exit_phase(obj);
}

static const TypeInfo resettable_interface_info = {
    .name       = TYPE_RESETTABLE,
    .parent     = TYPE_INTERFACE,
    .class_size = sizeof(ResettableClass),
};

static void reset_register_types(void)
{
    type_register_static(&resettable_interface_info);
}

type_init(reset_register_types)
