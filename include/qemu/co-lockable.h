/*
 * Polymorphic locking functions (aka poor man templates)
 *
 * Copyright Red Hat, Inc. 2017, 2018
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_CO_LOCKABLE_H
#define QEMU_CO_LOCKABLE_H

#include "qemu/coroutine.h"
#include "qemu/thread.h"

typedef CoroutineAction QemuCoLockUnlockFunc(void *);

struct QemuCoLockable {
    void *object;
    QemuCoLockUnlockFunc *lock;
    QemuCoLockUnlockFunc *unlock;
};

static inline CoroutineAction qemu_mutex_co_lock(QemuMutex *mutex)
{
    qemu_mutex_lock(mutex);
    return COROUTINE_CONTINUE;
}

static inline CoroutineAction qemu_mutex_co_unlock(QemuMutex *mutex)
{
    qemu_mutex_unlock(mutex);
    return COROUTINE_CONTINUE;
}

static inline __attribute__((__always_inline__)) QemuCoLockable *
qemu_make_co_lockable(void *x, QemuCoLockable *lockable)
{
    /*
     * We cannot test this in a macro, otherwise we get compiler
     * warnings like "the address of 'm' will always evaluate as 'true'".
     */
    return x ? lockable : NULL;
}

static inline __attribute__((__always_inline__)) QemuCoLockable *
qemu_null_co_lockable(void *x)
{
    if (x != NULL) {
        qemu_build_not_reached();
    }
    return NULL;
}

/*
 * In C, compound literals have the lifetime of an automatic variable.
 * In C++ it would be different, but then C++ wouldn't need QemuCoLockable
 * either...
 */
#define QMCL_OBJ_(x, name) (&(QemuCoLockable) {                         \
        .object = (x),                                                  \
        .lock = (QemuCoLockUnlockFunc *) qemu_ ## name ## _lock,        \
        .unlock = (QemuCoLockUnlockFunc *) qemu_ ## name ## _unlock     \
    })

/**
 * QEMU_MAKE_CO_LOCKABLE - Make a polymorphic QemuCoLockable
 *
 * @x: a lock object (currently one of QemuMutex, CoMutex).
 *
 * Returns a QemuCoLockable object that can be passed around
 * to a function that can operate with locks of any kind, or
 * NULL if @x is %NULL.
 *
 * Note the special case for void *, so that we may pass "NULL".
 */
#define QEMU_MAKE_CO_LOCKABLE(x)                                            \
    _Generic((x), QemuCoLockable *: (x),                                    \
             void *: qemu_null_co_lockable(x),                              \
             QemuMutex *: qemu_make_co_lockable(x, QMCL_OBJ_(x, mutex_co)), \
             CoMutex *: qemu_make_co_lockable(x, QMCL_OBJ_(x, co_mutex)))   \

/**
 * QEMU_MAKE_CO_LOCKABLE_NONNULL - Make a polymorphic QemuCoLockable
 *
 * @x: a lock object (currently one of QemuMutex, QemuRecMutex,
 *     CoMutex, QemuSpin).
 *
 * Returns a QemuCoLockable object that can be passed around
 * to a function that can operate with locks of any kind.
 */
#define QEMU_MAKE_CO_LOCKABLE_NONNULL(x)                        \
    _Generic((x), QemuCoLockable *: (x),                        \
                  QemuMutex *: QMCL_OBJ_(x, mutex_co),          \
                  CoMutex *: QMCL_OBJ_(x, co_mutex))

static inline CoroutineAction qemu_co_lockable_lock(QemuCoLockable *x)
{
    return x->lock(x->object);
}

static inline CoroutineAction qemu_co_lockable_unlock(QemuCoLockable *x)
{
    return x->unlock(x->object);
}

#endif
