/*
 * Stub for tcg_update_all_cflags(), for binaries that link util/log.c
 * or cpu-target.c but not TCG.
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
