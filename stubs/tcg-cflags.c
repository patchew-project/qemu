/*
 * Stubs for the cached cflags update hooks, for binaries that link
 * util/log.c or cpu-target.c without linking TCG.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "system/tcg.h"

void tcg_update_curr_cflags(CPUState *cpu)
{
}

void tcg_update_all_curr_cflags(void)
{
}
