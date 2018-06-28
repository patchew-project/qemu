/*
 * accel stubs
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/accel.h"

bool assert_accelerator_initialized(bool allowed)
{
    return allowed;
}
