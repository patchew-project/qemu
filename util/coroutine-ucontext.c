/*
 * ucontext coroutine initialization code
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2011  Kevin Wolf <kwolf@redhat.com>
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

/* XXX Is there a nicer way to disable glibc's stack check for longjmp? */
#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
#include "qemu/osdep.h"
#include <ucontext.h>
#include "qemu-common.h"
#include "qemu/coroutine_int.h"

#ifdef CONFIG_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

#ifdef CONFIG_COROUTINE_STACK_SIZE_DEBUG
#include "qemu/error-report.h"
#endif

typedef struct {
    Coroutine base;
    void *stack;
    sigjmp_buf env;

#ifdef CONFIG_VALGRIND_H
    unsigned int valgrind_stack_id;
#endif

} CoroutineUContext;

/**
 * Per-thread coroutine bookkeeping
 */
static __thread CoroutineUContext leader;
static __thread Coroutine *current;

#ifdef CONFIG_COROUTINE_STACK_SIZE_DEBUG
static uint32_t max_stack_usage;
#endif

/*
 * va_args to makecontext() must be type 'int', so passing
 * the pointer we need may require several int args. This
 * union is a quick hack to let us do that
 */
union cc_arg {
    void *p;
    int i[2];
};

static void coroutine_trampoline(int i0, int i1)
{
    union cc_arg arg;
    CoroutineUContext *self;
    Coroutine *co;

    arg.i[0] = i0;
    arg.i[1] = i1;
    self = arg.p;
    co = &self->base;

    /* Initialize longjmp environment and switch back the caller */
    if (!sigsetjmp(self->env, 0)) {
        siglongjmp(*(sigjmp_buf *)co->entry_arg, 1);
    }

    while (true) {
        co->entry(co->entry_arg);
        qemu_coroutine_switch(co, co->caller, COROUTINE_TERMINATE);
    }
}

#define COROUTINE_STACK_SIZE (1 << 16)

Coroutine *qemu_coroutine_new(void)
{
    CoroutineUContext *co;
    ucontext_t old_uc, uc;
    sigjmp_buf old_env;
    union cc_arg arg = {0};
#ifdef CONFIG_COROUTINE_STACK_SIZE_DEBUG
    void *ptr;
#endif

    /* The ucontext functions preserve signal masks which incurs a
     * system call overhead.  sigsetjmp(buf, 0)/siglongjmp() does not
     * preserve signal masks but only works on the current stack.
     * Since we need a way to create and switch to a new stack, use
     * the ucontext functions for that but sigsetjmp()/siglongjmp() for
     * everything else.
     */

    if (getcontext(&uc) == -1) {
        abort();
    }

    co = g_malloc0(sizeof(*co));

#ifdef MAP_GROWSDOWN
    co->stack = mmap(NULL, COROUTINE_STACK_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, -1, 0);
    if (co->stack == MAP_FAILED) {
        abort();
    }
    /* add a guard page at bottom of the stack */
    if (mmap(co->stack, getpagesize(), PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, -1, 0) == MAP_FAILED) {
        abort();
    }
#else
    co->stack = g_malloc(stack_size);
#endif

#ifdef CONFIG_COROUTINE_STACK_SIZE_DEBUG
    for (ptr = co->stack + getpagesize();
         ptr < co->stack + COROUTINE_STACK_SIZE; ptr += sizeof(u_int32_t)) {
        *(u_int32_t *)ptr = 0xdeadbeaf;
    }
#endif

    co->base.entry_arg = &old_env; /* stash away our jmp_buf */

    uc.uc_link = &old_uc;
    uc.uc_stack.ss_sp = co->stack;
    uc.uc_stack.ss_size = COROUTINE_STACK_SIZE;
    uc.uc_stack.ss_flags = 0;

#ifdef CONFIG_VALGRIND_H
    co->valgrind_stack_id =
        VALGRIND_STACK_REGISTER(co->stack, co->stack + COROUTINE_STACK_SIZE);
#endif

    arg.p = co;

    makecontext(&uc, (void (*)(void))coroutine_trampoline,
                2, arg.i[0], arg.i[1]);

    /* swapcontext() in, siglongjmp() back out */
    if (!sigsetjmp(old_env, 0)) {
        swapcontext(&old_uc, &uc);
    }
    return &co->base;
}

#ifdef CONFIG_VALGRIND_H
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
/* Work around an unused variable in the valgrind.h macro... */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
static inline void valgrind_stack_deregister(CoroutineUContext *co)
{
    VALGRIND_STACK_DEREGISTER(co->valgrind_stack_id);
}
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic pop
#endif
#endif

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineUContext *co = DO_UPCAST(CoroutineUContext, base, co_);

#ifdef CONFIG_COROUTINE_STACK_SIZE_DEBUG
    void *ptr;
    for (ptr = co->stack + getpagesize();
         ptr < co->stack + COROUTINE_STACK_SIZE; ptr += sizeof(u_int32_t)) {
        if (*(u_int32_t *)ptr != 0xdeadbeaf) {
            break;
        }
    }
    /* we only want to estimate the max stack usage, the OR will overestimate
     * the stack usage, but this is ok here and avoids the usage of a mutex */
    atomic_or(&max_stack_usage,
              COROUTINE_STACK_SIZE - (uintptr_t) (ptr - co->stack));
#endif

#ifdef CONFIG_VALGRIND_H
    valgrind_stack_deregister(co);
#endif

#ifdef MAP_GROWSDOWN
    munmap(co->stack, COROUTINE_STACK_SIZE);
#else
    g_free(co->stack);
#endif
    g_free(co);
}

/* This function is marked noinline to prevent GCC from inlining it
 * into coroutine_trampoline(). If we allow it to do that then it
 * hoists the code to get the address of the TLS variable "current"
 * out of the while() loop. This is an invalid transformation because
 * the sigsetjmp() call may be called when running thread A but
 * return in thread B, and so we might be in a different thread
 * context each time round the loop.
 */
CoroutineAction __attribute__((noinline))
qemu_coroutine_switch(Coroutine *from_, Coroutine *to_,
                      CoroutineAction action)
{
    CoroutineUContext *from = DO_UPCAST(CoroutineUContext, base, from_);
    CoroutineUContext *to = DO_UPCAST(CoroutineUContext, base, to_);
    int ret;

    current = to_;

    ret = sigsetjmp(from->env, 0);
    if (ret == 0) {
        siglongjmp(to->env, action);
    }
    return ret;
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

#ifdef CONFIG_COROUTINE_STACK_SIZE_DEBUG
static void __attribute__((destructor)) print_max_stack_usage(void)
{
   error_report("coroutine-ucontext: max stack usage was less or equal to "
                "%"PRIu32" bytes.", max_stack_usage);
}
#endif
