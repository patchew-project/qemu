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

/*
 * Hot-unplug a virtual (guest) CPU.
 *
 * Also triggered on each CPU when an instrumentation library is unloaded.
 *
 * Mode: user, softmmu
 * Targets: all
 * Time: exec
 */
void qi_event_set_guest_cpu_exit(void (*fn)(QICPU vcpu));

/*
 * Reset the state of a virtual (guest) CPU.
 *
 * Mode: user, softmmu
 * Targets: all
 * Time: exec
 */
void qi_event_set_guest_cpu_reset(void (*fn)(QICPU vcpu));

/*
 * Start virtual memory access (before any potential access violation).
 *
 * @vaddr: Access' virtual address.
 * @info : Access' information.
 *
 * Does not include memory accesses performed by devices.
 *
 * Mode: user, softmmu
 * Targets: TCG(all)
 * Time: trans
 */
void qi_event_set_guest_mem_before_trans(
    void (*fn)(QICPU vcpu_trans, QITCGv_cpu vcpu_exec,
               QITCGv vaddr, QIMemInfo info));

/*
 * Generate code to trigger a 'guest_mem_before_exec' from
 * 'guest_mem_before_trans'.
 *
 * Mode: user, softmmu
 * Targets: TCG(all)
 * Time: trans
 */
void qi_event_gen_guest_mem_before_exec(
    QITCGv_cpu vcpu, QITCGv vaddr, QIMemInfo info);

/*
 * Execution-time equivalent of 'guest_mem_before_trans'.
 *
 * Mode: user, softmmu
 * Targets: TCG(all)
 * Time: exec
 */
void qi_event_set_guest_mem_before_exec(
    void (*fn)(QICPU vcpu, uint64_t vaddr, QIMemInfo info));

/*
 * Start executing a guest system call in syscall emulation mode.
 *
 * @num: System call number.
 * @arg*: System call argument value.
 *
 * Mode: user
 * Targets: TCG(all)
 * Time: exec
 */
void qi_event_set_guest_user_syscall(
    void (*fn)(QICPU vcpu, uint64_t num, uint64_t arg1, uint64_t arg2,
               uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6,
               uint64_t arg7, uint64_t arg8));

/*
 * Finish executing a guest system call in syscall emulation mode.
 *
 * @num: System call number.
 * @ret: System call result value.
 *
 * Mode: user
 * Targets: TCG(all)
 * Time: exec
 */
void qi_event_set_guest_user_syscall_ret(
    void (*fn)(QICPU vcpu, uint64_t num, uint64_t ret));

#ifdef __cplusplus
}
#endif

#endif  /* QI__CONTROL_H */
