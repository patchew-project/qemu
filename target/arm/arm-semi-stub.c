/*
 * Arm "Angel" semihosting syscalls stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/semihosting/semihost.h"

target_ulong do_arm_semihosting(CPUARMState *env)
{
    abort();
}
