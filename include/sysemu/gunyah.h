/*
 * QEMU Gunyah hypervisor support
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* header to be included in non-Gunyah-specific code */

#ifndef QEMU_GUNYAH_H
#define QEMU_GUNYAH_H

#include "qemu/accel.h"
#include "qom/object.h"

#ifdef NEED_CPU_H
#include "cpu.h"
#endif

extern bool gunyah_allowed;

void gunyah_set_swiotlb_size(uint64_t size);

#define gunyah_enabled() (gunyah_allowed)

#define TYPE_GUNYAH_ACCEL ACCEL_CLASS_NAME("gunyah")
typedef struct GUNYAHState GUNYAHState;
DECLARE_INSTANCE_CHECKER(GUNYAHState, GUNYAH_STATE,
                         TYPE_GUNYAH_ACCEL)

int gunyah_arm_set_dtb(uint64_t dtb_start, uint64_t dtb_size);

#endif  /* QEMU_GUNYAH_H */
