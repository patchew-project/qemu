/*
 * QEMU Thread Local Storage for coroutines
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 * It is forbidden to access Thread Local Storage in coroutines because
 * compiler optimizations may cause values to be cached across coroutine
 * re-entry. Coroutines can run in more than one thread through the course of
 * their life, leading bugs when stale TLS values from the wrong thread are
 * used as a result of compiler optimization.
 *
 * An example is:
 *
 * ..code-block:: c
 *   :caption: A coroutine that may see the wrong TLS value
 *
 *   static __thread AioContext *current_aio_context;
 *   ...
 *   static void coroutine_fn foo(void)
 *   {
 *       aio_notify(current_aio_context);
 *       qemu_coroutine_yield();
 *       aio_notify(current_aio_context); // <-- may be stale after yielding!
 *   }
 *
 * This header provides macros for safely defining variables in Thread Local
 * Storage:
 *
 * ..code-block:: c
 *   :caption: A coroutine that safely uses TLS
 *
 *   QEMU_DEFINE_STATIC_CO_TLS(AioContext *, current_aio_context)
 *   ...
 *   static void coroutine_fn foo(void)
 *   {
 *       aio_notify(get_current_aio_context());
 *       qemu_coroutine_yield();
 *       aio_notify(get_current_aio_context()); // <-- safe
 *   }
 */

#ifndef QEMU_COROUTINE_TLS_H
#define QEMU_COROUTINE_TLS_H

/*
 * Two techniques are available to stop the compiler from caching TLS values:
 * 1. Accessor functions with __attribute__((noinline)). This is
 *    architecture-independent but prevents inlining optimizations.
 * 2. TLS address-of implemented as asm volatile so it can be inlined safely.
 *    This enables inlining optimizations but requires architecture-specific
 *    inline assembly.
 */
#if defined(__aarch64__)
#define QEMU_CO_TLS_ADDR(ret, var)                                           \
    asm volatile("mrs %0, tpidr_el0\n\t"                                     \
                 "add %0, %0, #:tprel_hi12:"#var", lsl #12\n\t"              \
                 "add %0, %0, #:tprel_lo12_nc:"#var                          \
                 : "=r"(ret))
#elif defined(__powerpc64__)
#define QEMU_CO_TLS_ADDR(ret, var)                                           \
    asm volatile("addis %0,13,"#var"@tprel@ha\n\t"                           \
                 "add   %0,%0,"#var"@tprel@l"                                \
                 : "=r"(ret))
#elif defined(__riscv)
#define QEMU_CO_TLS_ADDR(ret, var)                                           \
    asm volatile("lui  %0,%%tprel_hi("#var")\n\t"                            \
                 "add  %0,%0,%%tprel_add("#var")\n\t"                        \
                 "addi %0,%0,%%tprel_lo("#var")"                             \
                 : "=r"(ret))
#elif defined(__x86_64__)
#define QEMU_CO_TLS_ADDR(ret, var)                                           \
    asm volatile("movq %%fs:0, %0\n\t"                                       \
                 "lea "#var"@tpoff(%0), %0" : "=r"(ret))
#endif

/**
 * QEMU_DECLARE_CO_TLS:
 * @type: the variable's C type
 * @var: the variable name
 *
 * Declare an extern variable in Thread Local Storage from a header file:
 *
 * .. code-block:: c
 *   :caption: Declaring an extern variable in Thread Local Storage
 *
 *   QEMU_DECLARE_CO_TLS(int, my_count)
 *   ...
 *   int c = get_my_count();
 *   set_my_count(c + 1);
 *   *get_ptr_my_count() = 0;
 *
 * Use this instead of:
 *
 * .. code-block:: c
 *   :caption: Declaring a TLS variable using __thread
 *
 *   extern __thread int my_count;
 *   ...
 *   int c = my_count;
 *   my_count = c + 1;
 *   *(&my_count) = 0;
 */
#ifdef QEMU_CO_TLS_ADDR
#define QEMU_DECLARE_CO_TLS(type, var)                                       \
    extern __thread type co_tls_##var;                                       \
    static inline type get_##var(void)                                       \
    { type *p; QEMU_CO_TLS_ADDR(p, co_tls_##var); return *p; }               \
    static inline void set_##var(type v)                                     \
    { type *p; QEMU_CO_TLS_ADDR(p, co_tls_##var); *p = v; }                  \
    static inline type *get_ptr_##var(void)                                  \
    { type *p; QEMU_CO_TLS_ADDR(p, co_tls_##var); return p; }
#else
#define QEMU_DECLARE_CO_TLS(type, var)                                       \
    __attribute__((noinline)) type get_##var(void);                          \
    __attribute__((noinline)) void set_##var(type v);                        \
    __attribute__((noinline, weak)) type *get_ptr_##var(void);
#endif

/**
 * QEMU_DEFINE_CO_TLS:
 * @type: the variable's C type
 * @var: the variable name
 *
 * Define an variable in Thread Local Storage that was previously declared from
 * a header file with QEMU_DECLARE_CO_TLS():
 *
 * .. code-block:: c
 *   :caption: Defining a variable in Thread Local Storage
 *
 *   QEMU_DEFINE_CO_TLS(int, my_count)
 *
 * Use this instead of:
 *
 * .. code-block:: c
 *   :caption: Defining a TLS variable using __thread
 *
 *   __thread int my_count;
 */
#ifdef QEMU_CO_TLS_ADDR
#define QEMU_DEFINE_CO_TLS(type, var)                                        \
    __thread type co_tls_##var;
#else
#define QEMU_DEFINE_CO_TLS(type, var)                                        \
    static __thread type co_tls_##var;                                       \
    type get_##var(void) { return co_tls_##var; }                            \
    void set_##var(type v) { co_tls_##var = v; }                             \
    type *get_ptr_##var(void) { return &co_tls_##var; }
#endif

/**
 * QEMU_DEFINE_STATIC_CO_TLS:
 * @type: the variable's C type
 * @var: the variable name
 *
 * Define a static variable in Thread Local Storage:
 *
 * .. code-block:: c
 *   :caption: Defining a static variable in Thread Local Storage
 *
 *   QEMU_DEFINE_STATIC_CO_TLS(int, my_count)
 *   ...
 *   int c = get_my_count();
 *   set_my_count(c + 1);
 *   *get_ptr_my_count() = 0;
 *
 * Use this instead of:
 *
 * .. code-block:: c
 *   :caption: Defining a static TLS variable using __thread
 *
 *   static __thread int my_count;
 *   ...
 *   int c = my_count;
 *   my_count = c + 1;
 *   *(&my_count) = 0;
 */
#ifdef QEMU_CO_TLS_ADDR
#define QEMU_DEFINE_STATIC_CO_TLS(type, var)                                 \
    __thread type co_tls_##var;                                              \
    static __attribute__((unused)) inline type get_##var(void)               \
    { type *p; QEMU_CO_TLS_ADDR(p, co_tls_##var); return *p; }               \
    static __attribute__((unused)) inline void set_##var(type v)             \
    { type *p; QEMU_CO_TLS_ADDR(p, co_tls_##var); *p = v; }                  \
    static __attribute__((unused)) inline type *get_ptr_##var(void)          \
    { type *p; QEMU_CO_TLS_ADDR(p, co_tls_##var); return p; }
#else
#define QEMU_DEFINE_STATIC_CO_TLS(type, var)                                 \
    static __thread type co_tls_##var;                                       \
    static __attribute__((noinline, unused))                                 \
    type get_##var(void)                                                     \
    { return co_tls_##var; }                                                 \
    static __attribute__((noinline, unused))                                 \
    void set_##var(type v)                                                   \
    { co_tls_##var = v; }                                                    \
    static __attribute__((noinline, weak, unused))                           \
    type *get_ptr_##var(void)                                                \
    { return &co_tls_##var; }
#endif

#endif /* QEMU_COROUTINE_TLS_H */
