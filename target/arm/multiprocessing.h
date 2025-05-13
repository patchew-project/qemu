/*
 * ARM multiprocessor CPU helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TARGET_ARM_MULTIPROCESSING_H
#define TARGET_ARM_MULTIPROCESSING_H

#include "target/arm/cpu-qom.h"

#define ARM_AFF0_SHIFT 0
#define ARM_AFF0_MASK  (0xFFULL << ARM_AFF0_SHIFT)
#define ARM_AFF1_SHIFT 8
#define ARM_AFF1_MASK  (0xFFULL << ARM_AFF1_SHIFT)
#define ARM_AFF2_SHIFT 16
#define ARM_AFF2_MASK  (0xFFULL << ARM_AFF2_SHIFT)
#define ARM_AFF3_SHIFT 32
#define ARM_AFF3_MASK  (0xFFULL << ARM_AFF3_SHIFT)
#define ARM_DEFAULT_CPUS_PER_CLUSTER 8

#define ARM32_AFFINITY_MASK \
            (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK)
#define ARM64_AFFINITY_MASK \
            (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK|ARM_AFF3_MASK)
#define ARM64_AFFINITY_INVALID (~ARM64_AFFINITY_MASK)

uint64_t arm_build_mp_affinity(int idx, uint8_t clustersz);

uint64_t arm_cpu_mp_affinity(ARMCPU *cpu);

#endif
