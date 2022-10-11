/*
 * Common qemu-thread implementation header file.
 *
 * Copyright Red Hat, Inc. 2018
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_THREAD_COMMON_H
#define QEMU_THREAD_COMMON_H

#include "qemu/thread.h"
#include "trace.h"

static inline void qemu_mutex_post_init(QemuMutex *mutex)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = NULL;
    mutex->line = 0;
    mutex->locked = false;
#endif
    mutex->initialized = true;
}

static inline void qemu_mutex_pre_lock(QemuMutex *mutex,
                                       const char *file, int line)
{
    trace_qemu_mutex_lock(mutex, file, line);
}

static inline void qemu_mutex_post_lock(QemuMutex *mutex,
                                        const char *file, int line)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = file;
    mutex->line = line;
    mutex->locked = true;
#endif
    trace_qemu_mutex_locked(mutex, file, line);
}

static inline void qemu_mutex_pre_unlock(QemuMutex *mutex,
                                         const char *file, int line)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = NULL;
    mutex->line = 0;
    /*
     * pthread_mutex_unlock() by default silently ignore unlocking a mutex
     * even if it's not locked.  Make it strict with QEMU when DEBUG_MUTEX
     * is enabled, so that we can capture it at the exact wrong unlock.
     * It'll be easier to track this than having misterious deadlock later.
     */
    assert(mutex->locked);
    mutex->locked = false;
#endif
    trace_qemu_mutex_unlock(mutex, file, line);
}

#endif
