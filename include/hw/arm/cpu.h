/*
 * ARM / Aarch64 CPU definitions
 *
 * This file contains architectural definitions consumed by hardware models
 * implementations (files under hw/).
 * Definitions not required to be exposed to hardware has to go in the
 * architecture specific "target/arm/cpu.h" header.
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef HW_ARM_CPU_H
#define HW_ARM_CPU_H

#include "hw/core/cpu.h"

#define TYPE_ARM_CPU "arm-cpu"
OBJECT_DECLARE_CPU_TYPE(ARMCPU, ARMCPUClass, ARM_CPU)

#define TYPE_AARCH64_CPU "aarch64-cpu"
typedef struct AArch64CPUClass AArch64CPUClass;
DECLARE_CLASS_CHECKERS(AArch64CPUClass, AARCH64_CPU, TYPE_AARCH64_CPU)

#define ARM_CPU_TYPE_SUFFIX "-" TYPE_ARM_CPU
#define ARM_CPU_TYPE_NAME(name) (name ARM_CPU_TYPE_SUFFIX)

#endif
