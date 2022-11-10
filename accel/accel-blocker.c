/*
 * QEMU accel blocker class
 *
 * Copyright (c) 2014 Red Hat Inc.
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

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"
#include "sysemu/accel-blocker.h"

static QemuLockCnt accel_in_ioctl_lock;
static QemuEvent accel_in_ioctl_event;

void accel_blocker_init(void)
{
    qemu_lockcnt_init(&accel_in_ioctl_lock);
    qemu_event_init(&accel_in_ioctl_event, false);
}

void accel_set_in_ioctl(bool in_ioctl)
{
    if (likely(qemu_mutex_iothread_locked())) {
        return;
    }
    if (in_ioctl) {
        /* block if lock is taken in kvm_ioctl_inhibit_begin() */
        qemu_lockcnt_inc(&accel_in_ioctl_lock);
    } else {
        qemu_lockcnt_dec(&accel_in_ioctl_lock);
        /* change event to SET. If event was BUSY, wake up all waiters */
        qemu_event_set(&accel_in_ioctl_event);
    }
}

void accel_cpu_set_in_ioctl(CPUState *cpu, bool in_ioctl)
{
    if (unlikely(qemu_mutex_iothread_locked())) {
        return;
    }
    if (in_ioctl) {
        /* block if lock is taken in kvm_ioctl_inhibit_begin() */
        qemu_lockcnt_inc(&cpu->in_ioctl_lock);
    } else {
        qemu_lockcnt_dec(&cpu->in_ioctl_lock);
        /* change event to SET. If event was BUSY, wake up all waiters */
        qemu_event_set(&accel_in_ioctl_event);
    }
}

static int accel_in_ioctls(void)
{
    CPUState *cpu;
    int ret = qemu_lockcnt_count(&accel_in_ioctl_lock);

    CPU_FOREACH(cpu) {
        ret += qemu_lockcnt_count(&cpu->in_ioctl_lock);
    }

    return  ret;
}

void accel_ioctl_inhibit_begin(void)
{
    CPUState *cpu;

    /*
     * We allow to inhibit only when holding the BQL, so we can identify
     * when an inhibitor wants to issue an ioctl easily.
     */
    g_assert(qemu_mutex_iothread_locked());

    /* Block further invocations of the ioctls outside the BQL.  */
    CPU_FOREACH(cpu) {
        qemu_lockcnt_lock(&cpu->in_ioctl_lock);
    }
    qemu_lockcnt_lock(&accel_in_ioctl_lock);

    /* Keep waiting until there are running ioctls */
    while (accel_in_ioctls()) {
        /* Reset event to FREE. */
        qemu_event_reset(&accel_in_ioctl_event);

        if (accel_in_ioctls()) {

            CPU_FOREACH(cpu) {
                /* exit the ioctl */
                qemu_cpu_kick(cpu);
            }

            /*
             * If event is still FREE, and there are ioctls still in progress,
             * wait.
             *
             *  If an ioctl finishes before qemu_event_wait(), it will change
             * the event state to SET. This will prevent qemu_event_wait() from
             * blocking, but it's not a problem because if other ioctls are
             * still running (accel_in_ioctls is true) the loop will iterate
             * once more and reset the event status to FREE so that it can wait
             * properly.
             *
             * If an ioctls finishes while qemu_event_wait() is blocking, then
             * it will be waken up, but also here the while loop makes sure
             * to re-enter the wait if there are other running ioctls.
             */
            qemu_event_wait(&accel_in_ioctl_event);
        }
    }
}

void accel_ioctl_inhibit_end(void)
{
    CPUState *cpu;

    qemu_lockcnt_unlock(&accel_in_ioctl_lock);
    CPU_FOREACH(cpu) {
        qemu_lockcnt_unlock(&cpu->in_ioctl_lock);
    }
}

