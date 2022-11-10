/*
 * Accelerator blocking API, to prevent new ioctls from starting and wait the
 * running ones finish.
 * This mechanism differs from pause/resume_all_vcpus() in that it does not
 * release the BQL.
 *
 *  Copyright (c) 2014 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACCEL_BLOCKER_H
#define ACCEL_BLOCKER_H

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "sysemu/cpus.h"

extern void accel_blocker_init(void);

/*
 * accel_set_in_ioctl/accel_cpu_set_in_ioctl:
 * Mark when ioctl is about to run or just finished.
 * If @in_ioctl is true, then mark it is beginning. Otherwise marks that it is
 * ending.
 *
 * These functions will block after accel_ioctl_inhibit_begin() is called,
 * preventing new ioctls to run. They will continue only after
 * accel_ioctl_inibith_end().
 */
extern void accel_set_in_ioctl(bool in_ioctl);
extern void accel_cpu_set_in_ioctl(CPUState *cpu, bool in_ioctl);

/*
 * accel_ioctl_inhibit_begin/end: start/end critical section
 * Between these two calls, no ioctl marked with accel_set_in_ioctl() and
 * accel_cpu_set_in_ioctl() is allowed to run.
 *
 * This allows the caller to access shared data or perform operations without
 * worrying of concurrent vcpus accesses.
 */
extern void accel_ioctl_inhibit_begin(void);
extern void accel_ioctl_inhibit_end(void);

#endif /* ACCEL_BLOCKER_H */
