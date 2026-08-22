/*
 * Stubs for the TCG entry points in system/tcg.h, for binaries that link
 * util/log.c, cpu-target.c or cpu-common.c but not TCG.
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

void tcg_cpu_poison_jmp_cache(CPUState *cpu)
{
}
