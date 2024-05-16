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

void gunyah_set_swiotlb_size(uint64_t size)
{
    return;
}
