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

    /*
     * aarch64: instruction pointer
     */
    void *scratch;

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

#elif defined __aarch64__
/*
 * GCC does not support clobbering the frame pointer, so we save it ourselves.
 * Saving the link register as well generates slightly better code because then
 * qemu_coroutine_switch can be treated as a leaf procedure.
 */
#define CO_SWITCH_RET(from, to, action) ({                                            \
    register uintptr_t action_ __asm__("x0") = action;                                \
    register void *from_ __asm__("x16") = from;                                       \
    register void *to_ __asm__("x1") = to;                                            \
    asm volatile(                                                                     \
        ".cfi_remember_state\n"                                                       \
        "stp x29, x30, [sp, #-16]!\n"    /* GCC does not save it, do it ourselves */  \
        ".cfi_adjust_cfa_offset 16\n"                                                 \
        ".cfi_def_cfa_register sp\n"                                                  \
        "adr x30, 2f\n"                  /* source PC will be after the BR */         \
        "str x30, [x16, %[SCRATCH]]\n"   /* save it */                                \
        "mov x30, sp\n"                  /* save source SP */                         \
        "str x30, [x16, %[SP]]\n"                                                     \
        "ldr x30, [x1, %[SCRATCH]]\n"    /* load destination PC */                    \
        "ldr x1, [x1, %[SP]]\n"          /* load destination SP */                    \
        "mov sp, x1\n"                                                                \
        "br x30\n"                                                                    \
        "2: \n"                                                                       \
        "ldp x29, x30, [sp], #16\n"                                                   \
        ".cfi_restore_state\n"                                                        \
        : "+r" (action_), "+r" (from_), "+r" (to_)                                    \
        : [SP] "i" (offsetof(CoroutineAsm, sp)),                                      \
          [SCRATCH] "i" (offsetof(CoroutineAsm, scratch))                             \
        : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",        \
          "x13", "x14", "x15", "x17", "x18", "x19", "x20", "x21", "x22", "x23",       \
          "x24", "x25", "x26", "x27", "x28",                                          \
          "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",   \
          "v12", "v13", "v14", "v15", v16", "v17", "v18", "v19", "v20", "v21", "v22", \
          "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31", "memory",    \
    action_;                                                                          \
})

#define CO_SWITCH_NEW(from, to) do {                                                  \
  (to)->scratch = (void *) coroutine_trampoline;                                      \
  (void) CO_SWITCH_RET(from, to, (uintptr_t) to);                                     \
} while(0)
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
