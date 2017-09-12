/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "instrument/control.h"
#include "instrument/error.h"
#include "instrument/events.h"
#include "instrument/load.h"
#include "instrument/qemu-instr/control.h"
#include "qemu/compiler.h"

__thread InstrState instr_cur_state;


qi_fini_fn instr_event__fini_fn;
void *instr_event__fini_data;

SYM_PUBLIC void qi_set_fini(qi_fini_fn fn, void *data)
{
    ERROR_IF(!instr_get_state(), "called outside instrumentation");
    instr_set_event(fini_fn, fn);
    instr_set_event(fini_data, data);
}
