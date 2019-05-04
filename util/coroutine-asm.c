/*
 * Host-specific coroutine initialization code
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2011  Kevin Wolf <kwolf@redhat.com>
 * Copyright (C) 2019  Paolo Bonzini <pbonzini@redhat.com>
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
#include "qemu-common.h"
#include "qemu/coroutine_int.h"

#ifdef CONFIG_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#ifdef CONFIG_ASAN_IFACE_FIBER
#define CONFIG_ASAN 1
#include <sanitizer/asan_interface.h>
#endif
#endif

#define COROUTINE_SHADOW_STACK_SIZE    4096

typedef struct {
    Coroutine base;
    void *sp;

    void *stack;
    size_t stack_size;

#ifdef CONFIG_VALGRIND_H
    unsigned int valgrind_stack_id;
#endif
} CoroutineAsm;

/**
 * Per-thread coroutine bookkeeping
 */
static __thread CoroutineAsm leader;
static __thread Coroutine *current;

static void finish_switch_fiber(void *fake_stack_save)
{
#ifdef CONFIG_ASAN
    const void *bottom_old;
    size_t size_old;

    __sanitizer_finish_switch_fiber(fake_stack_save, &bottom_old, &size_old);

    if (!leader.stack) {
        leader.stack = (void *)bottom_old;
        leader.stack_size = size_old;
    }
#endif
}

static void start_switch_fiber(void **fake_stack_save,
                               const void *bottom, size_t size)
{
#ifdef CONFIG_ASAN
    __sanitizer_start_switch_fiber(fake_stack_save, bottom, size);
#endif
}

#ifdef __x86_64__
/*
 * We hardcode all operands to specific registers so that we can write down all the
 * others in the clobber list.  Note that action also needs to be hardcoded so that
 * it is the same register in all expansions of this macro.  Also, we use %rdi
 * for the coroutine because that is the ABI's first argument register;
 * coroutine_trampoline can then retrieve the current coroutine from there.
 *
 * Note that push and call would clobber the red zone.  Makefile.objs compiles this
 * file with -mno-red-zone.  The alternative is to subtract/add 128 bytes from rsp
 * around the switch, with slightly lower cache performance.
 */
#define CO_SWITCH(from, to, action, jump) ({                                          \
    int action_ = action;                                                             \
    void *from_ = from;                                                               \
    void *to_ = to;                                                                   \
    asm volatile(                                                                     \
        "pushq %%rbp\n"                     /* save frame register on source stack */ \
        ".cfi_adjust_cfa_offset 8\n"                                                  \
        "call 1f\n"                         /* switch continues at label 1 */         \
        "jmp 2f\n"                          /* switch back continues at label 2 */    \
                                                                                      \
        "1: .cfi_adjust_cfa_offset 8\n"                                               \
        "movq %%rsp, %c[SP](%[FROM])\n"     /* save source SP */                      \
        "movq %c[SP](%[TO]), %%rsp\n"       /* load destination SP */                 \
        jump "\n"                           /* coroutine switch */                    \
                                                                                      \
        "2: .cfi_adjust_cfa_offset -8\n"                                              \
        "popq %%rbp\n"                                                                \
        ".cfi_adjust_cfa_offset -8\n"                                                 \
        : "+a" (action_), [FROM] "+b" (from_), [TO] "+D" (to_)                        \
        : [SP] "i" (offsetof(CoroutineAsm, sp))                                       \
        : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",  \
          "memory");                                                                  \
    action_;                                                                          \
})
/* Use "call" to ensure the stack  is aligned correctly.  */
#define CO_SWITCH_NEW(from, to) CO_SWITCH(from, to, 0, "call coroutine_trampoline")
#define CO_SWITCH_RET(from, to, action) CO_SWITCH(from, to, action, "ret")
#else
#error coroutine-asm.c not ported to this architecture.
#endif

static void __attribute__((__used__)) coroutine_trampoline(CoroutineAsm *self)
{
    finish_switch_fiber(NULL);

    while (true) {
        Coroutine *co = &self->base;
        qemu_coroutine_switch(co, co->caller, COROUTINE_TERMINATE);
        co->entry(co->entry_arg);
    }
}

Coroutine *qemu_coroutine_new(void)
{
    CoroutineAsm *co;
    void *fake_stack_save = NULL;

    co = g_malloc0(sizeof(*co));
    co->stack_size = COROUTINE_STACK_SIZE;
    co->stack = qemu_alloc_stack(&co->stack_size);
    co->sp = co->stack + co->stack_size;

#ifdef CONFIG_VALGRIND_H
    co->valgrind_stack_id =
        VALGRIND_STACK_REGISTER(co->stack, co->stack + co->stack_size);
#endif

    /*
     * Immediately enter the coroutine once to initialize the stack
     * and program counter.  We could instead just push the address
     * of coroutine_trampoline and let qemu_coroutine_switch return
     * to it, but doing it this way confines the non-portable code
     * to the CO_SWITCH* macros.
     */
    co->base.caller = qemu_coroutine_self();
    start_switch_fiber(&fake_stack_save, co->stack, co->stack_size);
    CO_SWITCH_NEW(current, co);
    finish_switch_fiber(fake_stack_save);
    co->base.caller = NULL;

    return &co->base;
}

#ifdef CONFIG_VALGRIND_H
#if defined(CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE) && !defined(__clang__)
/* Work around an unused variable in the valgrind.h macro... */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
static inline void valgrind_stack_deregister(CoroutineAsm *co)
{
    VALGRIND_STACK_DEREGISTER(co->valgrind_stack_id);
}
#if defined(CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineAsm *co = DO_UPCAST(CoroutineAsm, base, co_);

#ifdef CONFIG_VALGRIND_H
    valgrind_stack_deregister(co);
#endif

    qemu_free_stack(co->stack, co->stack_size);
    g_free(co);
}

/*
 * This function is marked noinline to prevent GCC from inlining it
 * into coroutine_trampoline(). If we allow it to do that then it
 * hoists the code to get the address of the TLS variable "current"
 * out of the while() loop. This is an invalid transformation because
 * qemu_coroutine_switch() may be called when running thread A but
 * return in thread B, and so we might be in a different thread
 * context each time round the loop.
 */
CoroutineAction __attribute__((noinline))
qemu_coroutine_switch(Coroutine *from_, Coroutine *to_,
                      CoroutineAction action)
{
    CoroutineAsm *from = DO_UPCAST(CoroutineAsm, base, from_);
    CoroutineAsm *to = DO_UPCAST(CoroutineAsm, base, to_);
    void *fake_stack_save = NULL;

    current = to_;

    start_switch_fiber(action == COROUTINE_TERMINATE ?
                       NULL : &fake_stack_save, to->stack, to->stack_size);
    action = CO_SWITCH_RET(from, to, action);
    finish_switch_fiber(fake_stack_save);

    return action;
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
