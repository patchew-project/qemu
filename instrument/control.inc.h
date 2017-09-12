/*
 * Control instrumentation during program (de)initialization.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/atomic.h"
#include "qemu/compiler.h"
#include <stdbool.h>


extern __thread InstrState instr_cur_state;

static inline void instr_set_state(InstrState state)
{
    atomic_store_release(&instr_cur_state, state);
}

static inline InstrState instr_get_state(void)
{
    return atomic_load_acquire(&instr_cur_state);
}
