/*
 * API for QEMU's tracing events.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "instrument/error.h"
#include "qemu/compiler.h"
#include "qemu-instr/trace.h"
#include "trace/control.h"


SYM_PUBLIC
QITraceEvent *qi_trace_event_name(const char *name)
{
    ERROR_IF_RET(!name, NULL, "must provide a name");
    return (QITraceEvent *)trace_event_name(name);
}

SYM_PUBLIC
void qi_trace_event_iter_init(QITraceEventIter *iter, const char *pattern)
{
    TraceEventIter *iter_ = (TraceEventIter *)iter;
    ERROR_IF(!iter_, "must provide an iterator");
    trace_event_iter_init(iter_, pattern);
}

SYM_PUBLIC
QITraceEvent *qi_trace_event_iter_next(QITraceEventIter *iter)
{
    TraceEventIter *iter_ = (TraceEventIter *)iter;
    ERROR_IF_RET(!iter_, NULL, "must provide an iterator");
    return (QITraceEvent *)trace_event_iter_next(iter_);
}


SYM_PUBLIC
bool qi_trace_event_is_vcpu(QITraceEvent *ev)
{
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_is_vcpu(ev_);
}

SYM_PUBLIC
const char *qi_trace_event_get_name(QITraceEvent *ev)
{
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_get_name(ev_);
}


SYM_PUBLIC
bool qi_trace_event_get_state(QITraceEvent *ev)
{
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_get_state_static(ev_) &&
        trace_event_get_state_dynamic(ev_);
}

SYM_PUBLIC
bool qi_trace_event_get_vcpu_state(QICPU *vcpu, QITraceEvent *ev)
{
    CPUState *vcpu_ = (CPUState *)vcpu;
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!vcpu_, false, "must provide a vCPU");
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_get_state_static(ev_) &&
        trace_event_get_vcpu_state_dynamic(vcpu_, ev_);
}

SYM_PUBLIC
bool qi_trace_event_get_state_static(QITraceEvent *ev)
{
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_get_state_static(ev_);
}

SYM_PUBLIC
bool qi_trace_event_get_state_dynamic(QITraceEvent *ev)
{
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_get_state_dynamic(ev_);
}

SYM_PUBLIC
bool qi_trace_event_get_vcpu_state_dynamic(QICPU *vcpu, QITraceEvent *ev)
{
    CPUState *vcpu_ = (CPUState *)vcpu;
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF_RET(!vcpu_, false, "must provide a vCPU");
    ERROR_IF_RET(!ev_, false, "must provide an event");
    return trace_event_get_vcpu_state_dynamic(vcpu_, ev_);
}

SYM_PUBLIC
void qi_trace_event_set_state_dynamic(QITraceEvent *ev, bool state)
{
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF(!ev_, "must provide an event");
    ERROR_IF(!trace_event_get_state_static(ev_),
             "event must be statically enabled");
    trace_event_set_state_dynamic(ev_, state);
}

SYM_PUBLIC
void qi_trace_event_set_vcpu_state_dynamic(QICPU *vcpu,
                                           QITraceEvent *ev, bool state)
{
    CPUState *vcpu_ = (CPUState *)vcpu;
    TraceEvent *ev_ = (TraceEvent *)ev;
    ERROR_IF(!vcpu_, "must provide a vCPU");
    ERROR_IF(!ev_, "must provide an event");
    ERROR_IF(!trace_event_get_state_static(ev_),
             "event must be statically enabled");
    trace_event_set_vcpu_state_dynamic(vcpu_, ev_, state);
}
