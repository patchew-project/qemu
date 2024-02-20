/*
 *  Reset handlers.
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2016 Red Hat, Inc.
 * Copyright (c) 2024 Linaro, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_SYSEMU_RESET_H
#define QEMU_SYSEMU_RESET_H

#include "qapi/qapi-events-run-state.h"

typedef void QEMUResetHandler(void *opaque);

/**
 * qemu_register_reset: Register a callback for system reset
 * @func: function to call
 * @opaque: opaque data to pass to @func
 *
 * Register @func on the list of functions which are called when the
 * entire system is reset. The functions are called in the order in
 * which they are registered.
 *
 * In general this function should not be used in new code where possible;
 * for instance device model reset is better accomplished using the
 * methods on DeviceState.
 *
 * It is not permitted to register or unregister reset functions from
 * within the @func callback.
 *
 * We assume that the caller holds the BQL.
 */
void qemu_register_reset(QEMUResetHandler *func, void *opaque);

/**
 * qemu_register_reset_nosnapshotload: Register a callback for system reset
 * @func: function to call
 * @opaque: opaque data to pass to @func
 *
 * This is the same as qemu_register_reset(), except that @func is
 * not called if the reason that the system is being reset is to
 * put it into a clean state prior to loading a snapshot (i.e. for
 * SHUTDOWN_CAUSE_SNAPSHOT_LOAD).
 */
void qemu_register_reset_nosnapshotload(QEMUResetHandler *func, void *opaque);

/**
 * qemu_unregister_reset: Unregister a system reset callback
 * @func: function registered with qemu_register_reset()
 * @opaque: the same opaque data that was passed to qemu_register_reset()
 *
 * Undo the effects of a qemu_register_reset(). The @func and @opaque
 * must both match the arguments originally used with qemu_register_reset().
 *
 * We assume that the caller holds the BQL.
 */
void qemu_unregister_reset(QEMUResetHandler *func, void *opaque);

/**
 * qemu_devices_reset: Perform a complete system reset
 * @reason: reason for the reset
 *
 * This function performs the low-level work needed to do a complete reset
 * of the system (calling all the callbacks registered with
 * qemu_register_reset()). It should only be called by the code in a
 * MachineClass reset method.
 *
 * If you want to trigger a system reset from, for instance, a device
 * model, don't use this function. Use qemu_system_reset_request().
 */
void qemu_devices_reset(ShutdownCause reason);

#endif
