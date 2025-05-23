/*
 * VFIO based AP matrix device assignment
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Rorie Reyes <rreyes@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/s390x/ap-bridge.h"

int ap_chsc_sei_nt0_get_event(void *res)
{
    return 0;
}

int ap_chsc_sei_nt0_have_event(void)
{
    return 0;
}
