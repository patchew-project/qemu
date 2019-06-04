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

#define RESETTABLE_MAX_COUNT 50

#define RESETTABLE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ResettableClass, (obj), TYPE_RESETTABLE)

static void resettable_init_phase(Object *obj, bool cold);

static void resettable_cold_init_phase(Object *obj)
{
    resettable_init_phase(obj, true);
}

static void resettable_warm_init_phase(Object *obj)
{
    resettable_init_phase(obj, false);
}

static void resettable_init_phase(Object *obj, bool cold)
{
    void (*func)(Object *);
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    uint32_t count;

    count = rc->increment_count(obj);
    /* this assert is triggered by an eventual reset loop */
    assert(count <= RESETTABLE_MAX_COUNT);

    func = cold ? resettable_cold_init_phase : resettable_warm_init_phase;
    rc->foreach_child(obj, func);

    if (rc->phases.init) {
        rc->phases.init(obj, cold);
    }
}

static void resettable_hold_phase(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);

    rc->foreach_child(obj, resettable_hold_phase);

    if (rc->phases.hold) {
        rc->phases.hold(obj);
    }
}

static void resettable_exit_phase(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);

    rc->foreach_child(obj, resettable_exit_phase);

    assert(rc->get_count(obj) > 0);
    if (rc->decrement_count(obj) == 0 && rc->phases.exit) {
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

void resettable_reset(Object *obj, bool cold)
{
    resettable_assert_reset(obj, cold);
    resettable_deassert_reset(obj);
}

void resettable_class_set_parent_reset_phases(ResettableClass *rc,
                                              ResettableInitPhase init,
                                              ResettableHoldPhase hold,
                                              ResettableExitPhase exit,
                                              ResettablePhases *parent_phases)
{
    *parent_phases = rc->phases;
    if (init) {
        rc->phases.init = init;
    }
    if (hold) {
        rc->phases.hold = hold;
    }
    if (exit) {
        rc->phases.exit = exit;
    }
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
