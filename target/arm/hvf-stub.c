/*
 * QEMU Hypervisor.framework (HVF) stubs for ARM
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hvf_arm.h"

uint32_t hvf_arm_get_default_ipa_bit_size(void)
{
    g_assert_not_reached();
}

uint32_t hvf_arm_get_max_ipa_bit_size(void)
{
    g_assert_not_reached();
}

bool hvf_arm_el2_supported(void)
{
    g_assert_not_reached();
}

bool hvf_arm_el2_enabled(void)
{
    g_assert_not_reached();
}

void hvf_arm_el2_enable(bool)
{
    g_assert_not_reached();
}
