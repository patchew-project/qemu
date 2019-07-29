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

#define RESETTABLE_MAX_COUNT 50

#define RESETTABLE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ResettableClass, (obj), TYPE_RESETTABLE)

static void resettable_init_reset(Object *obj, bool cold);

static bool resettable_class_check(ResettableClass *rc)
{
    if (!rc->set_cold) {
        return false;
    }
    if (!rc->set_hold_needed) {
        return false;
    }
    if (!rc->increment_count) {
        return false;
    }
    if (!rc->decrement_count) {
        return false;
    }
    if (!rc->get_count) {
        return false;
    }
    return true;
}

static void resettable_foreach_child(ResettableClass *rc,
                                     Object *obj,
                                     void (*func)(Object *))
{
    if (rc->foreach_child) {
        rc->foreach_child(obj, func);
    }
}

static void resettable_init_cold_reset(Object *obj)
{
    resettable_init_reset(obj, true);
}

static void resettable_init_warm_reset(Object *obj)
{
    resettable_init_reset(obj, false);
}

static void resettable_init_reset(Object *obj, bool cold)
{
    void (*func)(Object *);
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    uint32_t count;
    bool action_needed = false;
    bool prev_cold;

    assert(resettable_class_check(rc));

    count = rc->increment_count(obj);
    /* this assert is here to catch an eventual reset loop */
    assert(count <= RESETTABLE_MAX_COUNT);

    /*
     * only take action if:
     * + we really enter reset for the 1st time
     * + or we are in warm reset and start a cold one
     */
    prev_cold = rc->set_cold(obj, cold);
    if (count == 1) {
        action_needed = true;
    } else if (cold && !prev_cold) {
        action_needed = true;
    }
    trace_resettable_phase_init(obj, object_get_typename(obj), cold, count,
                                action_needed);

    /* exec init phase */
    if (action_needed) {
        rc->set_hold_needed(obj, true);
        if (rc->phases.init) {
            rc->phases.init(obj);
        }
    }

    /*
     * handle the children even if action_needed is at false so that
     * children counts are incremented too
     */
    func = cold ? resettable_init_cold_reset : resettable_init_warm_reset;
    resettable_foreach_child(rc, obj, func);
    trace_resettable_phase_init_end(obj);
}

static void resettable_hold_reset(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    bool hold_needed;

    assert(resettable_class_check(rc));
    trace_resettable_phase_hold(obj, object_get_typename(obj));

    /* handle children first */
    resettable_foreach_child(rc, obj, resettable_hold_reset);

    /* exec hold phase */
    hold_needed = rc->set_hold_needed(obj, false);
    if (hold_needed) {
        if (rc->phases.hold) {
            rc->phases.hold(obj);
        }
    }
    trace_resettable_phase_hold_end(obj, hold_needed);
}

static void resettable_exit_reset(Object *obj)
{
    uint32_t count;
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);

    assert(resettable_class_check(rc));
    trace_resettable_phase_exit(obj, object_get_typename(obj));

    resettable_foreach_child(rc, obj, resettable_deassert_reset);

    count = rc->get_count(obj);
    /*
     * we could assert that count > 0 but there are some corner cases
     * where we prefer to let it go as it is probably harmless.
     * For example: if there is reset support addition between
     * hosts when doing a migration. We may do such things as
     * deassert a non-existing reset.
     */
    if (count > 0) {
        count = rc->decrement_count(obj);
    } else {
        trace_resettable_count_underflow(obj);
    }
    if (count == 0) {
        if (rc->phases.exit) {
            rc->phases.exit(obj);
        }
    }
    trace_resettable_phase_exit_end(obj, count);
}

void resettable_assert_reset(Object *obj, bool cold)
{
    trace_resettable_reset_assert(obj, object_get_typename(obj), cold);
    resettable_init_reset(obj, cold);
    resettable_hold_reset(obj);
}

void resettable_deassert_reset(Object *obj)
{
    trace_resettable_reset_deassert(obj, object_get_typename(obj));
    resettable_exit_reset(obj);
}

void resettable_reset(Object *obj, bool cold)
{
    trace_resettable_reset(obj, object_get_typename(obj), cold);
    resettable_assert_reset(obj, cold);
    resettable_deassert_reset(obj);
}

void resettable_reset_warm_fn(void *opaque)
{
    resettable_reset((Object *) opaque, false);
}

void resettable_reset_cold_fn(void *opaque)
{
    resettable_reset((Object *) opaque, true);
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
