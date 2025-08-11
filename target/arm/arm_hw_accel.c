/*
 * QEMU helpers for ARM hardware accelerators
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"

bool host_cpu_feature_supported(enum arm_features feat, bool can_emulate)
{
#if defined(CONFIG_KVM) || defined(CONFIG_HVF)
    static enum { F_UNKN, F_SUPP, F_UNSUPP } supported[64] = { };

    assert(feat < ARRAY_SIZE(supported));
    if (supported[feat] == F_UNKN) {
        supported[feat] = arm_hw_accel_cpu_feature_supported(feat, can_emulate);
    }
    return supported[feat] == F_SUPP;
#elif defined(CONFIG_TCG)
    return can_emulate;
#else
#error
#endif
}
