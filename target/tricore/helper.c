/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/registerfields.h"
#include "cpu.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "fpu/softfloat-helpers.h"
#include "qemu/qemu-print.h"

enum {
    TLBRET_MPX = -12,
    TLBRET_MPW = -11,
    TLBRET_MPR = -10,
    TLBRET_DIRTY = -4,
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

static uint32_t tricore_mpu_dpr_lower(CPUTriCoreState *env, int idx)
{
    switch (idx) {
    case 0:  return env->DPR0_0L;
    case 1:  return env->DPR0_1L;
    case 2:  return env->DPR0_2L;
    case 3:  return env->DPR0_3L;
    case 4:  return env->DPR1_0L;
    case 5:  return env->DPR1_1L;
    case 6:  return env->DPR1_2L;
    case 7:  return env->DPR1_3L;
    case 8:  return env->DPR2_0L;
    case 9:  return env->DPR2_1L;
    case 10: return env->DPR2_2L;
    case 11: return env->DPR2_3L;
    case 12: return env->DPR3_0L;
    case 13: return env->DPR3_1L;
    case 14: return env->DPR3_2L;
    case 15: return env->DPR3_3L;
    default: return 0;
    }
}

static uint32_t tricore_mpu_dpr_upper(CPUTriCoreState *env, int idx)
{
    switch (idx) {
    case 0:  return env->DPR0_0U;
    case 1:  return env->DPR0_1U;
    case 2:  return env->DPR0_2U;
    case 3:  return env->DPR0_3U;
    case 4:  return env->DPR1_0U;
    case 5:  return env->DPR1_1U;
    case 6:  return env->DPR1_2U;
    case 7:  return env->DPR1_3U;
    case 8:  return env->DPR2_0U;
    case 9:  return env->DPR2_1U;
    case 10: return env->DPR2_2U;
    case 11: return env->DPR2_3U;
    case 12: return env->DPR3_0U;
    case 13: return env->DPR3_1U;
    case 14: return env->DPR3_2U;
    case 15: return env->DPR3_3U;
    default: return 0;
    }
}

static uint32_t tricore_mpu_cpr_lower(CPUTriCoreState *env, int idx)
{
    switch (idx) {
    case 0:  return env->CPR0_0L;
    case 1:  return env->CPR0_1L;
    case 2:  return env->CPR0_2L;
    case 3:  return env->CPR0_3L;
    case 4:  return env->CPR1_0L;
    case 5:  return env->CPR1_1L;
    case 6:  return env->CPR1_2L;
    case 7:  return env->CPR1_3L;
    case 8:  return env->CPR2_0L;
    case 9:  return env->CPR2_1L;
    case 10: return env->CPR2_2L;
    case 11: return env->CPR2_3L;
    case 12: return env->CPR3_0L;
    case 13: return env->CPR3_1L;
    case 14: return env->CPR3_2L;
    case 15: return env->CPR3_3L;
    default: return 0;
    }
}

static uint32_t tricore_mpu_cpr_upper(CPUTriCoreState *env, int idx)
{
    switch (idx) {
    case 0:  return env->CPR0_0U;
    case 1:  return env->CPR0_1U;
    case 2:  return env->CPR0_2U;
    case 3:  return env->CPR0_3U;
    case 4:  return env->CPR1_0U;
    case 5:  return env->CPR1_1U;
    case 6:  return env->CPR1_2U;
    case 7:  return env->CPR1_3U;
    case 8:  return env->CPR2_0U;
    case 9:  return env->CPR2_1U;
    case 10: return env->CPR2_2U;
    case 11: return env->CPR2_3U;
    case 12: return env->CPR3_0U;
    case 13: return env->CPR3_1U;
    case 14: return env->CPR3_2U;
    case 15: return env->CPR3_3U;
    default: return 0;
    }
}

static uint32_t tricore_mpu_dpre(CPUTriCoreState *env, int prs)
{
    switch (prs) {
    case 0: return env->DPRE_0;
    case 1: return env->DPRE_1;
    case 2: return env->DPRE_2;
    case 3: return env->DPRE_3;
    default: return 0;
    }
}

static uint32_t tricore_mpu_dpwe(CPUTriCoreState *env, int prs)
{
    switch (prs) {
    case 0: return env->DPWE_0;
    case 1: return env->DPWE_1;
    case 2: return env->DPWE_2;
    case 3: return env->DPWE_3;
    default: return 0;
    }
}

static uint32_t tricore_mpu_cpxe(CPUTriCoreState *env, int prs)
{
    switch (prs) {
    case 0: return env->CPXE_0;
    case 1: return env->CPXE_1;
    case 2: return env->CPXE_2;
    case 3: return env->CPXE_3;
    default: return 0;
    }
}

static bool tricore_mpu_enabled(CPUTriCoreState *env)
{
    /*
     * The MPU is enabled when SYSCON.MPEN (bit 1) is set.
     * As a pragmatic fallback, also treat the MPU as enabled
     * when any enable bitmap is nonzero, since some RTOS ports
     * program the range/enable registers without setting SYSCON.MPEN.
     */
    if (env->SYSCON & MASK_SYSCON_PRO_TEN) {
        return true;
    }
    return (env->DPRE_0 | env->DPRE_1 | env->DPRE_2 | env->DPRE_3 |
            env->DPWE_0 | env->DPWE_1 | env->DPWE_2 | env->DPWE_3 |
            env->CPXE_0 | env->CPXE_1 | env->CPXE_2 | env->CPXE_3) != 0;
}

static int tricore_mpu_check(CPUTriCoreState *env, vaddr address,
                             MMUAccessType access_type, int *prot)
{
    int prs = (env->PSW & MASK_PSW_PRS) >> 12;
    uint32_t dpre = tricore_mpu_dpre(env, prs);
    uint32_t dpwe = tricore_mpu_dpwe(env, prs);
    uint32_t cpxe = tricore_mpu_cpxe(env, prs);
    int i;

    *prot = 0;

    /* Walk the 16 data protection ranges */
    for (i = 0; i < 16; i++) {
        uint32_t lower = tricore_mpu_dpr_lower(env, i);
        uint32_t upper = tricore_mpu_dpr_upper(env, i);

        if (address >= lower && address < upper) {
            if (dpre & (1u << i)) {
                *prot |= PAGE_READ;
            }
            if (dpwe & (1u << i)) {
                *prot |= PAGE_WRITE;
            }
        }
    }

    /* Walk the 16 code protection ranges */
    for (i = 0; i < 16; i++) {
        uint32_t lower = tricore_mpu_cpr_lower(env, i);
        uint32_t upper = tricore_mpu_cpr_upper(env, i);

        if (address >= lower && address < upper) {
            if (cpxe & (1u << i)) {
                *prot |= PAGE_EXEC;
            }
        }
    }

    /* Check the requested access against accumulated permissions */
    switch (access_type) {
    case MMU_DATA_LOAD:
        if (!(*prot & PAGE_READ)) {
            return TLBRET_MPR;
        }
        break;
    case MMU_DATA_STORE:
        if (!(*prot & PAGE_WRITE)) {
            return TLBRET_MPW;
        }
        break;
    case MMU_INST_FETCH:
        if (!(*prot & PAGE_EXEC)) {
            return TLBRET_MPX;
        }
        break;
    }

    return TLBRET_MATCH;
}

static int get_physical_address(CPUTriCoreState *env, hwaddr *physical,
                                int *prot, vaddr address,
                                MMUAccessType access_type, int mmu_idx)
{
    int ret = TLBRET_MATCH;

    *physical = address & 0xFFFFFFFF;
    *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    /*
     * Block instruction fetch from peripheral space
     * (segments 0xE and 0xF).
     */
    if (access_type == MMU_INST_FETCH &&
        (address & 0xE0000000) == 0xE0000000) {
        return TLBRET_MPX;
    }

    if (tricore_mpu_enabled(env)) {
        ret = tricore_mpu_check(env, address, access_type, prot);
    }

    return ret;
}

hwaddr tricore_cpu_get_phys_addr_debug(CPUState *cs, vaddr addr)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    hwaddr phys_addr;
    int prot;
    int mmu_idx = cpu_mmu_index(cs, false);

    if (get_physical_address(&cpu->env, &phys_addr, &prot, addr,
                             MMU_DATA_LOAD, mmu_idx)) {
        return -1;
    }
    return phys_addr;
}

static void raise_mmu_exception(CPUTriCoreState *env, vaddr address,
                                int rw, int tlb_error)
{
    CPUState *cs = env_cpu(env);

    switch (tlb_error) {
    case TLBRET_MPR:
        cs->exception_index = TRAPC_PROT;
        break;
    case TLBRET_MPW:
        cs->exception_index = TRAPC_PROT;
        break;
    case TLBRET_MPX:
        cs->exception_index = TRAPC_PROT;
        break;
    default:
        cs->exception_index = TRAPC_SYSBUS;
        break;
    }
}

bool tricore_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                          MMUAccessType rw, int mmu_idx,
                          bool probe, uintptr_t retaddr)
{
    CPUTriCoreState *env = cpu_env(cs);
    hwaddr physical;
    int prot;
    int ret;

    ret = get_physical_address(env, &physical, &prot,
                               address, rw, mmu_idx);

    qemu_log_mask(CPU_LOG_MMU, "%s address=0x%" VADDR_PRIx " ret %d physical "
                  HWADDR_FMT_plx " prot %d\n",
                  __func__, address, ret, physical, prot);

    if (ret == TLBRET_MATCH) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    } else {
        assert(ret < 0);
        if (probe) {
            return false;
        }
        raise_mmu_exception(env, address, rw, ret);
        cpu_loop_exit_restore(cs, retaddr);
    }
}

void fpu_set_state(CPUTriCoreState *env)
{
    switch (extract32(env->PSW, 24, 2)) {
    case 0:
        set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
        break;
    case 1:
        set_float_rounding_mode(float_round_up, &env->fp_status);
        break;
    case 2:
        set_float_rounding_mode(float_round_down, &env->fp_status);
        break;
    case 3:
        set_float_rounding_mode(float_round_to_zero, &env->fp_status);
        break;
    }

    set_flush_inputs_to_zero(1, &env->fp_status);
    set_flush_to_zero(1, &env->fp_status);
    set_float_detect_tininess(float_tininess_before_rounding, &env->fp_status);
    set_float_ftz_detection(float_ftz_before_rounding, &env->fp_status);
    set_default_nan_mode(1, &env->fp_status);
    /* Default NaN pattern: sign bit clear, frac msb set */
    set_float_default_nan_pattern(0b01000000, &env->fp_status);
}

uint32_t psw_read(CPUTriCoreState *env)
{
    /* clear all USB bits */
    env->PSW &= 0x7ffffff;
    /* now set them from the cache */
    env->PSW |= ((env->PSW_USB_C != 0) << 31);
    env->PSW |= ((env->PSW_USB_V   & (1 << 31))  >> 1);
    env->PSW |= ((env->PSW_USB_SV  & (1 << 31))  >> 2);
    env->PSW |= ((env->PSW_USB_AV  & (1 << 31))  >> 3);
    env->PSW |= ((env->PSW_USB_SAV & (1 << 31))  >> 4);

    return env->PSW;
}

void psw_write(CPUTriCoreState *env, uint32_t val)
{
    env->PSW_USB_C = (val & MASK_USB_C);
    env->PSW_USB_V = (val & MASK_USB_V) << 1;
    env->PSW_USB_SV = (val & MASK_USB_SV) << 2;
    env->PSW_USB_AV = (val & MASK_USB_AV) << 3;
    env->PSW_USB_SAV = (val & MASK_USB_SAV) << 4;
    env->PSW = val;

    fpu_set_state(env);
}

#define FIELD_GETTER_WITH_FEATURE(NAME, REG, FIELD, FEATURE)     \
uint32_t NAME(CPUTriCoreState *env)                             \
{                                                                \
    if (tricore_has_feature(env, TRICORE_FEATURE_##FEATURE)) {   \
        return FIELD_EX32(env->REG, REG, FIELD ## _ ## FEATURE); \
    }                                                            \
    return FIELD_EX32(env->REG, REG, FIELD ## _13);              \
}

#define FIELD_GETTER(NAME, REG, FIELD)       \
uint32_t NAME(CPUTriCoreState *env)         \
{                                            \
    return FIELD_EX32(env->REG, REG, FIELD); \
}

#define FIELD_SETTER_WITH_FEATURE(NAME, REG, FIELD, FEATURE)              \
void NAME(CPUTriCoreState *env, uint32_t val)                            \
{                                                                         \
    if (tricore_has_feature(env, TRICORE_FEATURE_##FEATURE)) {            \
        env->REG = FIELD_DP32(env->REG, REG, FIELD ## _ ## FEATURE, val); \
    } else {                                                              \
        env->REG = FIELD_DP32(env->REG, REG, FIELD ## _13, val);          \
    }                                                                     \
}

#define FIELD_SETTER(NAME, REG, FIELD)                \
void NAME(CPUTriCoreState *env, uint32_t val)        \
{                                                     \
    env->REG = FIELD_DP32(env->REG, REG, FIELD, val); \
}

FIELD_GETTER_WITH_FEATURE(pcxi_get_pcpn, PCXI, PCPN, 161)
FIELD_SETTER_WITH_FEATURE(pcxi_set_pcpn, PCXI, PCPN, 161)
FIELD_GETTER_WITH_FEATURE(pcxi_get_pie, PCXI, PIE, 161)
FIELD_SETTER_WITH_FEATURE(pcxi_set_pie, PCXI, PIE, 161)
FIELD_GETTER_WITH_FEATURE(pcxi_get_ul, PCXI, UL, 161)
FIELD_SETTER_WITH_FEATURE(pcxi_set_ul, PCXI, UL, 161)
FIELD_GETTER(pcxi_get_pcxs, PCXI, PCXS)
FIELD_GETTER(pcxi_get_pcxo, PCXI, PCXO)

FIELD_GETTER_WITH_FEATURE(icr_get_ie, ICR, IE, 161)
FIELD_SETTER_WITH_FEATURE(icr_set_ie, ICR, IE, 161)
FIELD_GETTER(icr_get_ccpn, ICR, CCPN)
FIELD_SETTER(icr_set_ccpn, ICR, CCPN)
