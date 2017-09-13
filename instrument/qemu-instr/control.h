/*
 * Main instrumentation interface for QEMU.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QI__CONTROL_H
#define QI__CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <qemu-instr/types.h>


/**
 * SECTION:control
 * @section_id: qi-control
 * @title: Event control API for QEMU event instrumentation
 */

typedef void (*qi_fini_fn)(void *arg);

/**
 * qi_set_fini:
 * @fn: Finalization function.
 * @data: Argument to pass to the finalization function.
 *
 * Set the function to call when finalizing (unloading) the instrumentation
 * library.
 *
 * NOTE: Calls to printf() might not be shown if the library is unloaded when
 *       QEMU terminates.
 */
void qi_set_fini(qi_fini_fn fn, void *data);


/*
 * Set callbacks for available events. Each event has a short description and
 * various indicators of when it can be triggered:
 *
 * - Mode :: user
 *   Triggered in QEMU user application emulation (e.g., linux-user).
 *
 * - Mode :: softmmy
 *   Triggered in QEMU full-system emulation.
 *
 *
 * - Targets :: all
 *   Triggered on all targets, both using TCG or native hardware virtualization
 *   (e.g., KVM).
 *
 * - Targets :: TCG(<arch>)
 *   Triggered on the given guest target architectures when executing with TCG
 *   (no native hardware virtualization).
 *
 *
 * - Time :: exec
 *   Triggered when the guest executes the described operation.
 *
 * - Time :: trans
 *   Triggered when QEMU translates a guest operation. This is only available
 *   when executing with TCG. Guest instructions are decompiled and translated
 *   into the intermediate TCG language (when "Time: trans" events are
 *   triggered). Then, the TCG compiler translates TCG code into the native host
 *   code that QEMU will execute to emulate the guest (when "Time: exec" events
 *   are triggered). As QEMU uses a cache of translated code, the same
 *   instruction might be translated more than once (when the cache overflows).
 */

/*
 * Hot-plug a new virtual (guest) CPU.
 *
 * Also triggered on each CPU when an instrumentation library is loaded.
 *
 * Mode: user, softmmu
 * Targets: all
 * Time: exec
 */
void qi_event_set_guest_cpu_enter(void (*fn)(QICPU vcpu));

#ifdef __cplusplus
}
#endif

#endif  /* QI__CONTROL_H */
