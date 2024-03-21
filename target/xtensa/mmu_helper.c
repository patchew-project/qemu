/*
 * Copyright (c) 2011 - 2019, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "cpu.h"
#include "mmu.h"

void HELPER(itlb_hit_test)(CPUXtensaState *env, uint32_t vaddr)
{
    /*
     * Probe the memory; we don't care about the result but
     * only the side-effects (ie any MMU or other exception)
     */
    probe_access(env, vaddr, 1, MMU_INST_FETCH,
                 cpu_mmu_index(env_cpu(env), true), GETPC());
}

void HELPER(wsr_rasid)(CPUXtensaState *env, uint32_t v)
{
    v = (v & 0xffffff00) | 0x1;
    if (v != env->sregs[RASID]) {
        env->sregs[RASID] = v;
        tlb_flush(env_cpu(env));
    }
}

uint32_t HELPER(rtlb0)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        const xtensa_tlb_entry *entry = xtensa_get_tlb_entry(env, v, dtlb, &wi);

        if (entry) {
            return (entry->vaddr & xtensa_get_vpn_mask(env, dtlb, wi))
                   | entry->asid;
        } else {
            return 0;
        }
    } else {
        return v & REGION_PAGE_MASK;
    }
}

uint32_t HELPER(rtlb1)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    const xtensa_tlb_entry *entry = xtensa_get_tlb_entry(env, v, dtlb, NULL);

    if (entry) {
        return entry->paddr | entry->attr;
    } else {
        return 0;
    }
}

void HELPER(itlb)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        xtensa_tlb_entry *entry = xtensa_get_tlb_entry(env, v, dtlb, &wi);
        if (entry && entry->variable && entry->asid) {
            tlb_flush_page(env_cpu(env), entry->vaddr);
            entry->asid = 0;
        }
    }
}

uint32_t HELPER(ptlb)(CPUXtensaState *env, uint32_t v, uint32_t dtlb)
{
    if (xtensa_option_enabled(env->config, XTENSA_OPTION_MMU)) {
        uint32_t wi;
        uint32_t ei;
        uint8_t ring;
        int res = xtensa_tlb_lookup(env, v, dtlb, &wi, &ei, &ring);

        switch (res) {
        case 0:
            if (ring >= xtensa_get_ring(env)) {
                return (v & 0xfffff000) | wi | (dtlb ? 0x10 : 0x8);
            }
            break;

        case INST_TLB_MULTI_HIT_CAUSE:
        case LOAD_STORE_TLB_MULTI_HIT_CAUSE:
            HELPER(exception_cause_vaddr)(env, env->pc, res, v);
            break;
        }
        return 0;
    } else {
        return (v & REGION_PAGE_MASK) | 0x1;
    }
}

void HELPER(wtlb)(CPUXtensaState *env, uint32_t p, uint32_t v, uint32_t dtlb)
{
    uint32_t vpn;
    uint32_t wi;
    uint32_t ei;
    if (xtensa_split_tlb_entry_spec(env, v, dtlb, &vpn, &wi, &ei)) {
        xtensa_tlb_set_entry(env, dtlb, wi, ei, vpn, p);
    }
}

void HELPER(wsr_mpuenb)(CPUXtensaState *env, uint32_t v)
{
    v &= (2u << (env->config->n_mpu_fg_segments - 1)) - 1;

    if (v != env->sregs[MPUENB]) {
        env->sregs[MPUENB] = v;
        tlb_flush(env_cpu(env));
    }
}

void HELPER(wptlb)(CPUXtensaState *env, uint32_t p, uint32_t v)
{
    unsigned segment = p & XTENSA_MPU_SEGMENT_MASK;

    if (segment < env->config->n_mpu_fg_segments) {
        env->mpu_fg[segment].vaddr = v & -env->config->mpu_align;
        env->mpu_fg[segment].attr = p & XTENSA_MPU_ATTR_MASK;
        env->sregs[MPUENB] = deposit32(env->sregs[MPUENB], segment, 1, v);
        tlb_flush(env_cpu(env));
    }
}

uint32_t HELPER(rptlb0)(CPUXtensaState *env, uint32_t s)
{
    unsigned segment = s & XTENSA_MPU_SEGMENT_MASK;

    if (segment < env->config->n_mpu_fg_segments) {
        return env->mpu_fg[segment].vaddr |
            extract32(env->sregs[MPUENB], segment, 1);
    } else {
        return 0;
    }
}

uint32_t HELPER(rptlb1)(CPUXtensaState *env, uint32_t s)
{
    unsigned segment = s & XTENSA_MPU_SEGMENT_MASK;

    if (segment < env->config->n_mpu_fg_segments) {
        return env->mpu_fg[segment].attr;
    } else {
        return 0;
    }
}

uint32_t HELPER(pptlb)(CPUXtensaState *env, uint32_t v)
{
    unsigned nhits;
    unsigned segment = XTENSA_MPU_PROBE_B;
    unsigned bg_segment;

    nhits = xtensa_mpu_lookup(env->mpu_fg, env->config->n_mpu_fg_segments,
                              v, &segment);
    if (nhits > 1) {
        HELPER(exception_cause_vaddr)(env, env->pc,
                                      LOAD_STORE_TLB_MULTI_HIT_CAUSE, v);
    } else if (nhits == 1 && (env->sregs[MPUENB] & (1u << segment))) {
        return env->mpu_fg[segment].attr | segment | XTENSA_MPU_PROBE_V;
    } else {
        xtensa_mpu_lookup(env->config->mpu_bg,
                          env->config->n_mpu_bg_segments,
                          v, &bg_segment);
        return env->config->mpu_bg[bg_segment].attr | segment;
    }
}
