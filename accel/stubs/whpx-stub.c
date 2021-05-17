/*
 * QEMU WHPX stub
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "sysemu/whpx.h"

bool whpx_enabled(void)
{
    return false;
}

bool whpx_apic_in_platform(void)
{
    return false;
}
