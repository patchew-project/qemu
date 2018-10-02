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

const VMStateDescription vmstate_clockin = {
    .name = "clockin",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(frequency, ClockIn),
        VMSTATE_END_OF_LIST()
    }
};

#define CLOCK_PATH(_clk) (_clk->canonical_path)

void clock_out_setup_canonical_path(ClockOut *clk)
{
    g_free(clk->canonical_path);
    clk->canonical_path = object_get_canonical_path(OBJECT(clk));
}

void clock_in_setup_canonical_path(ClockIn *clk)
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

void clock_init_frequency(ClockIn *clk, uint64_t freq)
{
    assert(clk);

    clk->frequency = freq;
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

void clock_set_frequency(ClockOut *clk, uint64_t freq)
{
    ClockIn *follower;
    trace_clock_update(CLOCK_PATH(clk), freq);

    QLIST_FOREACH(follower, &clk->followers, sibling) {
        trace_clock_propagate(CLOCK_PATH(clk), CLOCK_PATH(follower));
        if (follower->frequency != freq) {
            follower->frequency = freq;
            if (follower->callback) {
                follower->callback(follower->callback_opaque);
            }
        }
    }
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

    g_free(clk->canonical_path);
    clk->canonical_path = NULL;
}

static void clock_in_finalizefn(Object *obj)
{
    ClockIn *clk = CLOCK_IN(obj);

    /* remove us from driver's followers list */
    clock_disconnect(clk);

    g_free(clk->canonical_path);
    clk->canonical_path = NULL;
}

static const TypeInfo clock_out_info = {
    .name              = TYPE_CLOCK_OUT,
    .parent            = TYPE_OBJECT,
    .instance_size     = sizeof(ClockOut),
    .instance_init     = clock_out_initfn,
    .instance_finalize = clock_out_finalizefn,
};

static const TypeInfo clock_in_info = {
    .name              = TYPE_CLOCK_IN,
    .parent            = TYPE_OBJECT,
    .instance_size     = sizeof(ClockIn),
    .instance_finalize = clock_in_finalizefn,
};

static void clock_register_types(void)
{
    type_register_static(&clock_in_info);
    type_register_static(&clock_out_info);
}

type_init(clock_register_types)
