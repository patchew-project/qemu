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
    abort();
}

void HELPER(v7m_blxns)(CPUARMState *env, uint32_t dest)
{
    abort();
}

uint32_t HELPER(v7m_mrs)(CPUARMState *env, uint32_t reg)
{
    abort();
}

void HELPER(v7m_msr)(CPUARMState *env, uint32_t maskreg, uint32_t val)
{
    abort();
}

uint32_t HELPER(v7m_tt)(CPUARMState *env, uint32_t addr, uint32_t op)
{
    abort();
}

void HELPER(v7m_preserve_fp_state)(CPUARMState *env)
{
    abort();
}

void write_v7m_exception(CPUARMState *env, uint32_t new_exc)
{
    abort();
}

void HELPER(v7m_vlldm)(CPUARMState *env, uint32_t fptr)
{
    abort();
}

void HELPER(v7m_vlstm)(CPUARMState *env, uint32_t fptr)
{
    abort();
}

ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    abort();
}
