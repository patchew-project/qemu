/*
 * Stubs for the TCG entry points in system/tcg.h, for binaries that link
 * cpu-target.c or the HMP command handlers but not TCG.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "system/tcg.h"

void tcg_update_cflags(CPUState *cpu)
{
}

void tcg_update_all_cflags(void)
{
}
