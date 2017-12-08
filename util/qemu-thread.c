/*
 * Extern inline functions for QemuLockGuard.
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"

QEMU_EXTERN_INLINE(qemu_lock_guard_cleanup)
QEMU_EXTERN_INLINE(qemu_lock_guard_pass)
