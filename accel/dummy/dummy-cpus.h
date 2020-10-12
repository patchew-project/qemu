/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef DUMMY_CPUS_H
#define DUMMY_CPUS_H

#include "qemu/typedefs.h"

void dummy_start_vcpu_thread(CPUState *);

#endif /* DUMMY_CPUS_H */
