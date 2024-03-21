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
#include "qemu/qemu-print.h"
#include "qemu/units.h"
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

static void dump_tlb(CPUXtensaState *env, bool dtlb)
{
    unsigned wi, ei;
    const xtensa_tlb *conf =
        dtlb ? &env->config->dtlb : &env->config->itlb;
    unsigned (*attr_to_access)(uint32_t) =
        xtensa_option_enabled(env->config, XTENSA_OPTION_MMU) ?
        mmu_attr_to_access : region_attr_to_access;

    qemu_printf("%s:\n", dtlb ? "DTLB" : "IBLB");
    for (wi = 0; wi < conf->nways; ++wi) {
        uint32_t sz = ~xtensa_tlb_get_addr_mask(env, dtlb, wi) + 1;
        const char *sz_text;
        bool print_header = true;

        if (sz >= 0x100000) {
            sz /= MiB;
            sz_text = "MB";
        } else {
            sz /= KiB;
            sz_text = "KB";
        }

        for (ei = 0; ei < conf->way_size[wi]; ++ei) {
            const xtensa_tlb_entry *entry =
                xtensa_tlb_get_entry(env, dtlb, wi, ei);

            if (entry->asid) {
                static const char * const cache_text[8] = {
                    [PAGE_CACHE_BYPASS >> PAGE_CACHE_SHIFT] = "Bypass",
                    [PAGE_CACHE_WT >> PAGE_CACHE_SHIFT] = "WT",
                    [PAGE_CACHE_WB >> PAGE_CACHE_SHIFT] = "WB",
                    [PAGE_CACHE_ISOLATE >> PAGE_CACHE_SHIFT] = "Isolate",
                };
                unsigned access = attr_to_access(entry->attr);
                unsigned cache_idx = (access & PAGE_CACHE_MASK) >>
                    PAGE_CACHE_SHIFT;

                if (print_header) {
                    print_header = false;
                    qemu_printf("Way %u (%d %s)\n", wi, sz, sz_text);
                    qemu_printf("\tVaddr       Paddr       ASID  Attr RWX Cache\n"
                                "\t----------  ----------  ----  ---- --- -------\n");
                }
                qemu_printf("\t0x%08x  0x%08x  0x%02x  0x%02x %c%c%c %s\n",
                            entry->vaddr,
                            entry->paddr,
                            entry->asid,
                            entry->attr,
                            (access & PAGE_READ) ? 'R' : '-',
                            (access & PAGE_WRITE) ? 'W' : '-',
                            (access & PAGE_EXEC) ? 'X' : '-',
                            cache_text[cache_idx] ?
                            cache_text[cache_idx] : "Invalid");
            }
        }
    }
}

static void dump_mpu(CPUXtensaState *env, const char *map_desc,
                     const xtensa_mpu_entry *entry, unsigned n)
{
    unsigned i;

    qemu_printf("%s map:\n", map_desc);
    qemu_printf("\t%s  Vaddr       Attr        Ring0  Ring1  System Type    CPU cache\n"
                "\t%s  ----------  ----------  -----  -----  -------------  ---------\n",
                env ? "En" : "  ",
                env ? "--" : "  ");

    for (i = 0; i < n; ++i) {
        uint32_t attr = entry[i].attr;
        unsigned access0 = mpu_attr_to_access(attr, 0);
        unsigned access1 = mpu_attr_to_access(attr, 1);
        unsigned type = mpu_attr_to_type(attr);
        char cpu_cache = (type & XTENSA_MPU_TYPE_CPU_CACHE) ? '-' : ' ';

        qemu_printf("\t %c  0x%08x  0x%08x   %c%c%c    %c%c%c   ",
                    env ?
                    ((env->sregs[MPUENB] & (1u << i)) ? '+' : '-') : ' ',
                    entry[i].vaddr, attr,
                    (access0 & PAGE_READ) ? 'R' : '-',
                    (access0 & PAGE_WRITE) ? 'W' : '-',
                    (access0 & PAGE_EXEC) ? 'X' : '-',
                    (access1 & PAGE_READ) ? 'R' : '-',
                    (access1 & PAGE_WRITE) ? 'W' : '-',
                    (access1 & PAGE_EXEC) ? 'X' : '-');

        switch (type & XTENSA_MPU_SYSTEM_TYPE_MASK) {
        case XTENSA_MPU_SYSTEM_TYPE_DEVICE:
            qemu_printf("Device %cB %3s\n",
                        (type & XTENSA_MPU_TYPE_B) ? ' ' : 'n',
                        (type & XTENSA_MPU_TYPE_INT) ? "int" : "");
            break;
        case XTENSA_MPU_SYSTEM_TYPE_NC:
            qemu_printf("Sys NC %cB      %c%c%c\n",
                        (type & XTENSA_MPU_TYPE_B) ? ' ' : 'n',
                        (type & XTENSA_MPU_TYPE_CPU_R) ? 'r' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_W) ? 'w' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_C) ? 'c' : cpu_cache);
            break;
        case XTENSA_MPU_SYSTEM_TYPE_C:
            qemu_printf("Sys  C %c%c%c     %c%c%c\n",
                        (type & XTENSA_MPU_TYPE_SYS_R) ? 'R' : '-',
                        (type & XTENSA_MPU_TYPE_SYS_W) ? 'W' : '-',
                        (type & XTENSA_MPU_TYPE_SYS_C) ? 'C' : '-',
                        (type & XTENSA_MPU_TYPE_CPU_R) ? 'r' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_W) ? 'w' : cpu_cache,
                        (type & XTENSA_MPU_TYPE_CPU_C) ? 'c' : cpu_cache);
            break;
        default:
            qemu_printf("Unknown\n");
            break;
        }
    }
}

void xtensa_dump_mmu(CPUXtensaState *env)
{
    if (xtensa_option_bits_enabled(env->config,
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION) |
                XTENSA_OPTION_BIT(XTENSA_OPTION_MMU))) {

        dump_tlb(env, false);
        qemu_printf("\n");
        dump_tlb(env, true);
    } else if (xtensa_option_enabled(env->config, XTENSA_OPTION_MPU)) {
        dump_mpu(env, "Foreground",
                 env->mpu_fg, env->config->n_mpu_fg_segments);
        qemu_printf("\n");
        dump_mpu(NULL, "Background",
                 env->config->mpu_bg, env->config->n_mpu_bg_segments);
    } else {
        qemu_printf("No TLB for this CPU core\n");
    }
}
