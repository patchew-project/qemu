/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/units.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#include "cpu.h"
#include "mmu.h"


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

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

    if (!env1) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    xtensa_dump_mmu(env1);
}
