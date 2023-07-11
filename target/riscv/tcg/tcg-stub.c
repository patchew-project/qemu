/*
 * QEMU RISC-V TCG stubs
 *
 * Copyright (c) 2023 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"

target_ulong riscv_cpu_get_fflags(CPURISCVState *env)
{
    g_assert_not_reached();
}

void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong)
{
    g_assert_not_reached();
}

G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc)
{
    g_assert_not_reached();
}

hwaddr riscv_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    /* XXX too many TCG code in the real riscv_cpu_get_phys_page_debug() */
    return -1;
}
