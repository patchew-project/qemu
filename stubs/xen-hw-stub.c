/*
 * Copyright (C) 2014       Citrix Systems UK Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/xen/xen.h"

int xen_is_pirq_msi(uint32_t msi_data)
{
    return 0;
}

void xen_register_framebuffer(MemoryRegion *mr)
{
}
