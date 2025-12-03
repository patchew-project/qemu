/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "migration/cpr.h"

bool cpr_is_incoming(void)
{
    return false;
}
