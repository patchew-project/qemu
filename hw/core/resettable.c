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
#include "trace.h"

#define RESETTABLE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ResettableClass, (obj), TYPE_RESETTABLE_INTERFACE)

static void resettable_foreach_child(ResettableClass *rc,
                                     Object *obj,
                                     void (*func)(Object *, ResetType type),
                                     ResetType type)
{
    if (rc->foreach_child) {
        rc->foreach_child(obj, func, type);
    }
}

static void resettable_init_reset(Object *obj, ResetType type)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResetState *s = rc->get_state(obj);
    bool action_needed = false;

    /* Only take action if we really enter reset for the 1st time. */
    /*
     * TODO: if adding more ResetType support, some additional checks
     * are probably needed here.
     */
    if (s->count == 0) {
        action_needed = true;
    }
    s->count += 1;
    /*
     * We limit the count to an arbitrary "big" value. The value is big
     * enough not to be triggered nominally.
     * The assert will stop an infinite loop if there is a cycle in the
     * reset tree. The loop goes through resettable_foreach_child below
     * which at some point will call us again.
     */
    assert(s->count <= 50);
    trace_resettable_phase_init(obj, object_get_typename(obj), type,
                                s->count, action_needed);

    /*
     * handle the children even if action_needed is at false so that
     * children counts are incremented too
     */
    resettable_foreach_child(rc, obj, resettable_init_reset, type);

    /* exec init phase */
    if (action_needed) {
        s->hold_phase_needed = true;
        if (rc->phases.init) {
            rc->phases.init(obj, type);
        }
    }
    trace_resettable_phase_init_end(obj);
}

static void resettable_hold_reset_child(Object *obj, ResetType type)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResetState *s = rc->get_state(obj);

    trace_resettable_phase_hold(obj, object_get_typename(obj));

    /* handle children first */
    resettable_foreach_child(rc, obj, resettable_hold_reset_child, 0);

    /* exec hold phase */
    if (s->hold_phase_needed) {
        s->hold_phase_needed = false;
        if (rc->phases.hold) {
            rc->phases.hold(obj);
        }
    }
    trace_resettable_phase_hold_end(obj, s->hold_phase_needed);
}

static void resettable_hold_reset(Object *obj)
{
    /* we don't care of 2nd argument here */
    resettable_hold_reset_child(obj, 0);
}

static void resettable_exit_reset_child(Object *obj, ResetType type)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResetState *s = rc->get_state(obj);

    trace_resettable_phase_exit(obj, object_get_typename(obj));

    resettable_foreach_child(rc, obj, resettable_exit_reset_child, 0);

    /*
     * we could assert that count > 0 but there are some corner cases
     * where we prefer to let it go as it is probably harmless.
     * For example: if there is reset support addition between
     * hosts when doing a migration. We may do such things as
     * deassert a non-existing reset.
     */
    if (s->count > 0) {
        s->count -= 1;
    } else {
        trace_resettable_count_underflow(obj);
    }
    if (s->count == 0) {
        if (rc->phases.exit) {
            rc->phases.exit(obj);
        }
    }
    trace_resettable_phase_exit_end(obj, s->count);
}

static void resettable_exit_reset(Object *obj)
{
    /* we don't care of 2nd argument here */
    resettable_exit_reset_child(obj, 0);
}

void resettable_reset(Object *obj, ResetType type)
{
    /* TODO: change that when adding support for other reset types */
    assert(type == RESET_TYPE_COLD);
    trace_resettable_reset(obj, type);
    resettable_init_reset(obj, type);
    resettable_hold_reset(obj);
    resettable_exit_reset(obj);
}

void resettable_cold_reset_fn(void *opaque)
{
    resettable_reset((Object *) opaque, RESET_TYPE_COLD);
}

bool resettable_is_resetting(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResetState *s = rc->get_state(obj);

    return (s->count > 0);
}

void resettable_class_set_parent_phases(ResettableClass *rc,
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
    .name       = TYPE_RESETTABLE_INTERFACE,
    .parent     = TYPE_INTERFACE,
    .class_size = sizeof(ResettableClass),
};

static void reset_register_types(void)
{
    type_register_static(&resettable_interface_info);
}

type_init(reset_register_types)
