/*
 * Clock inputs and outputs
 *
 * Copyright GreenSocs 2016-2018
 *
 * Authors:
 *  Frederic Konrad <fred.konrad@greensocs.com>
 *  Damien Hedde <damien.hedde@greensocs.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/clock-port.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"

#define CLOCK_PATH(_clk) (_clk->parent_obj.canonical_path)

void clock_setup_canonical_path(ClockPort *clk)
{
    g_free(clk->canonical_path);
    clk->canonical_path = object_get_canonical_path(OBJECT(clk));
}

void clock_set_callback(ClockIn *clk, ClockCallback *cb, void *opaque)
{
    assert(clk);

    clk->callback = cb;
    clk->callback_opaque = opaque;
}

void clock_clear_callback(ClockIn *clk)
{
    clock_set_callback(clk, NULL, NULL);
}

void clock_connect(ClockIn *clkin, ClockOut *clkout)
{
    assert(clkin && clkin->driver == NULL);
    assert(clkout);

    trace_clock_connect(CLOCK_PATH(clkin), CLOCK_PATH(clkout));

    QLIST_INSERT_HEAD(&clkout->followers, clkin, sibling);
    clkin->driver = clkout;
}

static void clock_disconnect(ClockIn *clk)
{
    if (clk->driver == NULL) {
        return;
    }

    trace_clock_disconnect(CLOCK_PATH(clk));

    clk->driver = NULL;
    QLIST_REMOVE(clk, sibling);
}

void clock_set(ClockOut *clk, ClockState *state)
{
    ClockIn *follower;
    trace_clock_update(CLOCK_PATH(clk), state->frequency, state->domain_reset);

    QLIST_FOREACH(follower, &clk->followers, sibling) {
        trace_clock_update(CLOCK_PATH(follower), state->frequency,
                state->domain_reset);
        if (follower->callback) {
            follower->callback(follower->callback_opaque, state);
        }
    }
}

static void clock_port_finalizefn(Object *obj)
{
    ClockPort *clk = CLOCK_PORT(obj);

    g_free(clk->canonical_path);
    clk->canonical_path = NULL;
}

static void clock_out_initfn(Object *obj)
{
    ClockOut *clk = CLOCK_OUT(obj);

    QLIST_INIT(&clk->followers);
}

static void clock_out_finalizefn(Object *obj)
{
    ClockOut *clk = CLOCK_OUT(obj);
    ClockIn *follower, *next;

    /* clear our list of followers */
    QLIST_FOREACH_SAFE(follower, &clk->followers, sibling, next) {
        clock_disconnect(follower);
    }
}

static void clock_in_finalizefn(Object *obj)
{
    ClockIn *clk = CLOCK_IN(obj);

    /* remove us from driver's followers list */
    clock_disconnect(clk);
}

static const TypeInfo clock_port_info = {
    .name              = TYPE_CLOCK_PORT,
    .parent            = TYPE_OBJECT,
    .abstract          = true,
    .instance_size     = sizeof(ClockPort),
    .instance_finalize = clock_port_finalizefn,
};

static const TypeInfo clock_out_info = {
    .name              = TYPE_CLOCK_OUT,
    .parent            = TYPE_CLOCK_PORT,
    .instance_size     = sizeof(ClockOut),
    .instance_init     = clock_out_initfn,
    .instance_finalize = clock_out_finalizefn,
};

static const TypeInfo clock_in_info = {
    .name              = TYPE_CLOCK_IN,
    .parent            = TYPE_CLOCK_PORT,
    .instance_size     = sizeof(ClockIn),
    .instance_finalize = clock_in_finalizefn,
};

static void clock_register_types(void)
{
    type_register_static(&clock_port_info);
    type_register_static(&clock_in_info);
    type_register_static(&clock_out_info);
}

type_init(clock_register_types)
