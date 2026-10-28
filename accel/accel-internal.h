/*
 * QEMU accel internal functions
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ACCEL_INTERNAL_H
#define ACCEL_INTERNAL_H

#include "qemu/accel.h"
#include "qapi/qapi-types-machine.h"

void accel_init_ops_interfaces(AccelClass *ac);
void accel_register_cpu_type(SysEmuTarget arch, const char *cpu_type);

#endif /* ACCEL_SYSTEM_H */
