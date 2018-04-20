/*
 * Common qemu-thread implementation shared for all platforms.
 *
 * Copyright Red Hat, Inc. 2018
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-thread-common.h"
#include "trace.h"

void qemu_mutex_pre_lock(QemuMutex *mutex, const char *file, int line)
{
    trace_qemu_mutex_lock(mutex, file, line);
}

void qemu_mutex_post_lock(QemuMutex *mutex, const char *file, int line)
{
    trace_qemu_mutex_locked(mutex, file, line);
}

void qemu_mutex_pre_unlock(QemuMutex *mutex, const char *file, int line)
{
    trace_qemu_mutex_unlock(mutex, file, line);
}
