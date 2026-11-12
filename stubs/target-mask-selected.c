/*
 * Tools have no selected target.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"

uint64_t target_mask_selected(void)
{
    return 0;
}
