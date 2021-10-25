/*
 * QEMU Thread Local Storage
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 * It is forbidden to use __thread in QEMU because compiler optimizations may
 * cause Thread Local Storage variables to be cached across coroutine re-entry.
 * Coroutines can run in more than one thread through the course of their life,
 * leading bugs when stale TLS values from the wrong thread are used as a
 * result of compiler optimization.
 *
 * Although avoiding __thread is strictly only necessary when coroutines access
 * the variable this is hard to audit or enforce in practice. Therefore
 * __thread is forbidden everywhere. This prevents subtle bugs from creeping in
 * if a variable that was previously not accessed from coroutines is now
 * accessed from coroutines.
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
 *   QEMU_DECLARE_STATIC_TLS(AioContext *, current_aio_context)
 *   ...
 *   static void coroutine_fn foo(void)
 *   {
 *       aio_notify(get_current_aio_context());
 *       qemu_coroutine_yield();
 *       aio_notify(get_current_aio_context()); // <-- safe
 *   }
 */

#ifndef QEMU_TLS_H
#define QEMU_TLS_H

/**
 * QEMU_DECLARE_TLS:
 * @type: the variable's C type
 * @var: the variable name
 *
 * Declare an extern variable in Thread Local Storage from a header file:
 *
 * .. code-block:: c
 *   :caption: Declaring an extern variable in Thread Local Storage
 *
 *   QEMU_DECLARE_TLS(int, my_count)
 *   ...
 *   int c = get_my_count();
 *   set_my_count(c + 1);
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
 */
#define QEMU_DECLARE_TLS(type, var) \
    __attribute__((noinline)) type get_##var(void); \
    __attribute__((noinline)) void set_##var(type v); \

/**
 * QEMU_DEFINE_TLS:
 * @type: the variable's C type
 * @var: the variable name
 *
 * Define an variable in Thread Local Storage that was previously declared from
 * a header file with QEMU_DECLARE_TLS():
 *
 * .. code-block:: c
 *   :caption: Defining a variable in Thread Local Storage
 *
 *   QEMU_DEFINE_TLS(int, my_count)
 *
 * Use this instead of:
 *
 * .. code-block:: c
 *   :caption: Defining a TLS variable using __thread
 *
 *   __thread int my_count;
 */
#define QEMU_DEFINE_TLS(type, var) \
    __thread type qemu_tls_##var; \
    type get_##var(void) { return qemu_tls_##var; } \
    void set_##var(type v) { qemu_tls_##var = v; }

/**
 * QEMU_DEFINE_STATIC_TLS:
 * @type: the variable's C type
 * @var: the variable name
 *
 * Define a static variable in Thread Local Storage:
 *
 * .. code-block:: c
 *   :caption: Defining a static variable in Thread Local Storage
 *
 *   QEMU_DEFINE_STATIC_TLS(int, my_count)
 *   ...
 *   int c = get_my_count();
 *   set_my_count(c + 1);
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
 */
#define QEMU_DEFINE_STATIC_TLS(type, var) \
    static __thread type qemu_tls_##var; \
    static __attribute__((noinline)) type get_##var(void); \
    static type get_##var(void) { return qemu_tls_##var; } \
    static __attribute__((noinline)) void set_##var(type v); \
    static void set_##var(type v) { qemu_tls_##var = v; }

#endif /* QEMU_TLS_H */
