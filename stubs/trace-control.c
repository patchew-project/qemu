/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2014-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "trace/control.h"


void trace_event_set_state_dynamic(uint16_t *dstate, TraceEvent *ev, bool state)
{
    assert(trace_event_get_state_static(ev));
}

void trace_event_set_vcpu_state_dynamic(uint16_t *dstate, CPUState *vcpu,
                                        TraceEvent *ev, bool state)
{
    /* should never be called on non-target binaries */
    abort();
}
