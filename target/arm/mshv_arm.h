/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2026
 *
 * Authors: Aastha Rawat   <aastharawat@linux.microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_MSHV_ARM_H
#define QEMU_MSHV_ARM_H

#include "target/arm/cpu.h"

void mshv_arm_set_cpu_features_from_host(ARMCPU *cpu);

#endif
