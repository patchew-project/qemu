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

#ifndef __QEMU_THREAD_COMMON_H__
#define __QEMU_THREAD_COMMON_H__

#include "qemu/typedefs.h"
#include "qemu/thread.h"

void qemu_mutex_pre_lock(QemuMutex *mutex, const char *file, int line);
void qemu_mutex_post_lock(QemuMutex *mutex, const char *file, int line);
void qemu_mutex_pre_unlock(QemuMutex *mutex, const char *file, int line);

#endif
