/*
 * Accelerator interface, specializes CPUClass
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ACCEL_CPU_H
#define ACCEL_CPU_H

/*
 * these defines cannot be in cpu.h, because we are using
 * CPU_RESOLVING_TYPE here.
 * Use this header to define your accelerator-specific
 * cpu-specific accelerator interfaces.
 */

#define TYPE_ACCEL_CPU "accel-" CPU_RESOLVING_TYPE
#define ACCEL_CPU_NAME(name) (name "-" TYPE_ACCEL_CPU)
typedef struct AccelCPUClass AccelCPUClass;
DECLARE_CLASS_CHECKERS(AccelCPUClass, ACCEL_CPU, TYPE_ACCEL_CPU)

#endif /* ACCEL_CPU_H */
