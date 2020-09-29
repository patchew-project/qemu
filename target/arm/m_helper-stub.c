/*
 * ARM V7M related stubs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "internals.h"

void HELPER(v7m_bxns)(CPUARMState *env, uint32_t dest)
{
    g_assert_not_reached();
}

void HELPER(v7m_blxns)(CPUARMState *env, uint32_t dest)
{
    g_assert_not_reached();
}

uint32_t HELPER(v7m_mrs)(CPUARMState *env, uint32_t reg)
{
    g_assert_not_reached();
}

void HELPER(v7m_msr)(CPUARMState *env, uint32_t maskreg, uint32_t val)
{
    g_assert_not_reached();
}

uint32_t HELPER(v7m_tt)(CPUARMState *env, uint32_t addr, uint32_t op)
{
    g_assert_not_reached();
}

void HELPER(v7m_preserve_fp_state)(CPUARMState *env)
{
    g_assert_not_reached();
}

void write_v7m_exception(CPUARMState *env, uint32_t new_exc)
{
    g_assert_not_reached();
}

void HELPER(v7m_vlldm)(CPUARMState *env, uint32_t fptr)
{
    g_assert_not_reached();
}

void HELPER(v7m_vlstm)(CPUARMState *env, uint32_t fptr)
{
    g_assert_not_reached();
}

ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    g_assert_not_reached();
}

#ifndef CONFIG_USER_ONLY

bool armv7m_nvic_can_take_pending_exception(void *opaque)
{
    g_assert_not_reached();
}

void arm_v7m_cpu_do_interrupt(CPUState *cs)
{
    g_assert_not_reached();
}

#endif /* CONFIG_USER_ONLY */
