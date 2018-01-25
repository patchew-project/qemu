/*
 * Polymorphic locking functions (aka poor man templates)
 *
 * Copyright Red Hat, Inc. 2017-2018
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_LOCKABLE_H
#define QEMU_LOCKABLE_H

#include "qemu/coroutine.h"
#include "qemu/thread.h"

typedef void QemuLockUnlockFunc(void *);

struct QemuLockable {
    void *object;
    QemuLockUnlockFunc *lock;
    QemuLockUnlockFunc *unlock;
};

/* This function gives link-time errors if an invalid, non-NULL
 * pointer type is passed to QEMU_MAKE_LOCKABLE.
 */
void unknown_lock_type(void *);

/* Auxiliary macros to simplify QEMU_MAKE_LOCKABLE.  */
#define QEMU_LOCK_FUNC(x) ((QemuLockUnlockFunc *)    \
    QEMU_GENERIC(x,                                  \
                 (QemuMutex *, qemu_mutex_lock),     \
                 (CoMutex *, qemu_co_mutex_lock),    \
                 (QemuSpin *, qemu_spin_lock),       \
                 ((x) ? unknown_lock_type : NULL)))

#define QEMU_UNLOCK_FUNC(x) ((QemuLockUnlockFunc *)  \
    QEMU_GENERIC(x,                                  \
                 (QemuMutex *, qemu_mutex_unlock),   \
                 (CoMutex *, qemu_co_mutex_unlock),  \
                 (QemuSpin *, qemu_spin_unlock),     \
                 ((x) ? unknown_lock_type : NULL)))

#define QEMU_MAKE_LOCKABLE_(x) (&(QemuLockable) {    \
        .object = (x),                               \
        .lock = QEMU_LOCK_FUNC(x),                   \
        .unlock = QEMU_UNLOCK_FUNC(x),               \
    })

/* QEMU_MAKE_LOCKABLE - Make a polymorphic QemuLockable
 *
 * @x: a lock object (currently one of QemuMutex, CoMutex, QemuSpin).
 *
 * Returns a QemuLockable object that can be passed around
 * to a function that can operate with locks of any kind.
 */
#define QEMU_MAKE_LOCKABLE(x)                        \
    QEMU_GENERIC(x,                                  \
                 (QemuLockable *, (x)),              \
                 QEMU_MAKE_LOCKABLE_(x))

static inline void qemu_lockable_lock(QemuLockable *x)
{
    x->lock(x);
}

static inline void qemu_lockable_unlock(QemuLockable *x)
{
    x->unlock(x);
}

#endif
