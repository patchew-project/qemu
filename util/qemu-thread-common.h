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
#include "trace/recorder.h"

RECORDER_DEFINE(lock, 16, "Lock state");

static inline void qemu_mutex_post_init(QemuMutex *mutex)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = NULL;
    mutex->line = 0;
#endif
    mutex->initialized = true;
    record(lock, "Init state %d for %p", -1, mutex);
}

static inline void qemu_mutex_pre_lock(QemuMutex *mutex,
                                       const char *file, int line)
{
    trace_qemu_mutex_lock(mutex, file, line);
    record(lock, "Locking state %d for %p", 1, mutex);
}

static inline void qemu_mutex_post_lock(QemuMutex *mutex,
                                        const char *file, int line)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = file;
    mutex->line = line;
#endif
    trace_qemu_mutex_locked(mutex, file, line);
    record(lock, "Locked state %d for %p", 2, mutex);
}

static inline void qemu_mutex_pre_unlock(QemuMutex *mutex,
                                         const char *file, int line)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = NULL;
    mutex->line = 0;
#endif
    trace_qemu_mutex_unlock(mutex, file, line);
    record(lock, "Unkocked state %d for %p", 0, mutex);
}

#endif
