/*
 * stackless coroutine initialization code
 *
 * Copyright (C) 2022 Paolo BOnzini <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "qemu/coroutine_int.h"

typedef struct {
    Coroutine base;
    void *stack;
    void *stack_ptr;
    CoroutineImpl *current_func;
    void *current_frame;
} CoroutineStackless;

static __thread CoroutineStackless leader;
static __thread Coroutine *current;

static void *coroutine_stack_alloc(CoroutineStackless *co, CoroutineImpl *func, size_t bytes)
{
    CoroutineFrame *ret = co->stack_ptr;

    bytes = ROUND_UP(bytes, 16);
    assert(bytes <= COROUTINE_STACK_SIZE - (co->stack_ptr - co->stack));
    co->stack_ptr += bytes;
    ret->caller_func = co->current_func;
    ret->caller_frame = co->current_frame;
    co->current_func = func;
    co->current_frame = ret;
    return ret;
}

static void coroutine_stack_free(CoroutineStackless *co, CoroutineFrame *f)
{
    assert((void *)f >= co->stack && (void *)f < co->stack_ptr);
    co->current_func = f->caller_func;
    co->current_frame = f->caller_frame;
    co->stack_ptr = f;
}

struct FRAME__coroutine_trampoline {
    CoroutineFrame common;
    bool back;
};

static CoroutineAction coroutine_trampoline(void *_frame)
{
    struct FRAME__coroutine_trampoline *_f = _frame;
    Coroutine *co = current;
    if (!_f->back) {
        _f->back = true;
        // or:
        //   if (co->entry(co->entry_arg) == COROUTINE_YIELD) return COROUTINE_YIELD;
        return co->entry(co->entry_arg);
    }

    _f->back = false;
    current = co->caller;
    co->caller = NULL;
    return COROUTINE_TERMINATE;
}

Coroutine *qemu_coroutine_new(void)
{
    CoroutineStackless *co;
    struct FRAME__coroutine_trampoline *frame;

    co = g_malloc0(sizeof(*co));
    co->stack = g_malloc(COROUTINE_STACK_SIZE);
    co->stack_ptr = co->stack;

    frame = coroutine_stack_alloc(co, coroutine_trampoline, sizeof(*frame));
    frame->back = false;
    return &co->base;
}

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineStackless *co = DO_UPCAST(CoroutineStackless, base, co_);
    struct FRAME__coroutine_trampoline *frame = co->current_frame;

    assert(!frame->back);
    coroutine_stack_free(co, co->current_frame);
    assert(co->stack_ptr == co->stack);
    g_free(co->stack);
    g_free(co);
}

CoroutineAction
qemu_coroutine_switch(Coroutine *from, Coroutine *to,
                      CoroutineAction action)
{
    assert(action == COROUTINE_ENTER);
    assert(to->caller != NULL);
    current = to;
    do {
        CoroutineStackless *co = DO_UPCAST(CoroutineStackless, base, to);
        action = co->current_func(co->current_frame);
    } while (action == COROUTINE_CONTINUE);
    assert(action != COROUTINE_ENTER);
    return action;
}

CoroutineAction qemu_coroutine_yield(void)
{
    Coroutine *from = current;
    Coroutine *to = from->caller;
    trace_qemu_coroutine_yield(from, to);
    if (!to) {
        fprintf(stderr, "Co-routine is yielding to no one\n");
        abort();
    }
    from->caller = NULL;
    current = to;
    return COROUTINE_YIELD;
}

Coroutine *qemu_coroutine_self(void)
{
    if (!current) {
        current = &leader.base;
    }
    return current;
}

bool qemu_in_coroutine(void)
{
    return current && current->caller;
}

void *stack_alloc(CoroutineImpl *func, size_t bytes)
{
    CoroutineStackless *co = DO_UPCAST(CoroutineStackless, base, current);

    return coroutine_stack_alloc(co, func, bytes);
}

CoroutineAction stack_free(CoroutineFrame *f)
{
    CoroutineStackless *co = DO_UPCAST(CoroutineStackless, base, current);
    coroutine_stack_free(co, f);
    return COROUTINE_CONTINUE;
}
