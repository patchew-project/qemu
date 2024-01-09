/*
 * QEMU Gunyah stub
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "sysemu/gunyah.h"

bool gunyah_allowed;

int gunyah_arm_set_dtb(__u64 dtb_start, __u64 dtb_size)
{
    return -1;
}
