/*
 * QEMU x86 MMU monitor commands
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "cpu.h"

hwaddr addr_canonical(CPUArchState *env, hwaddr addr)
{
#ifdef TARGET_X86_64
    if (env->cr[4] & CR4_LA57_MASK) {
        if (addr & (1ULL << 56)) {
            addr |= (hwaddr)-(1LL << 57);
        }
    } else {
        if (addr & (1ULL << 47)) {
            addr |= (hwaddr)-(1LL << 48);
        }
    }
#endif
    return addr;
}

static void print_pte(Monitor *mon, CPUArchState *env, hwaddr addr,
                      hwaddr pte, hwaddr mask)
{
    addr = addr_canonical(env, addr);

    monitor_printf(mon, HWADDR_FMT_plx ": " HWADDR_FMT_plx
                   " %c%c%c%c%c%c%c%c%c\n",
                   addr,
                   pte & mask,
                   pte & PG_NX_MASK ? 'X' : '-',
                   pte & PG_GLOBAL_MASK ? 'G' : '-',
                   pte & PG_PSE_MASK ? 'P' : '-',
                   pte & PG_DIRTY_MASK ? 'D' : '-',
                   pte & PG_ACCESSED_MASK ? 'A' : '-',
                   pte & PG_PCD_MASK ? 'C' : '-',
                   pte & PG_PWT_MASK ? 'T' : '-',
                   pte & PG_USER_MASK ? 'U' : '-',
                   pte & PG_RW_MASK ? 'W' : '-');
}

static void tlb_info_32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2;
    uint32_t pgd, pde, pte;

    pgd = env->cr[3] & ~0xfff;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, &pde, 4);
        pde = le32_to_cpu(pde);
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                /* 4M pages */
                print_pte(mon, env, (l1 << 22), pde, ~((1 << 21) - 1));
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4, &pte, 4);
                    pte = le32_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        print_pte(mon, env, (l1 << 22) + (l2 << 12),
                                  pte & ~PG_PSE_MASK,
                                  ~0xfff);
                    }
                }
            }
        }
    }
}

static void tlb_info_pae32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2, l3;
    uint64_t pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;

    pdp_addr = env->cr[3] & ~0x1f;
    for (l1 = 0; l1 < 4; l1++) {
        cpu_physical_memory_read(pdp_addr + l1 * 8, &pdpe, 8);
        pdpe = le64_to_cpu(pdpe);
        if (pdpe & PG_PRESENT_MASK) {
            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pd_addr + l2 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                if (pde & PG_PRESENT_MASK) {
                    if (pde & PG_PSE_MASK) {
                        /* 2M pages with PAE, CR4.PSE is ignored */
                        print_pte(mon, env, (l1 << 30) + (l2 << 21), pde,
                                  ~((hwaddr)(1 << 20) - 1));
                    } else {
                        pt_addr = pde & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pt_addr + l3 * 8, &pte, 8);
                            pte = le64_to_cpu(pte);
                            if (pte & PG_PRESENT_MASK) {
                                print_pte(mon, env, (l1 << 30) + (l2 << 21)
                                          + (l3 << 12),
                                          pte & ~PG_PSE_MASK,
                                          ~(hwaddr)0xfff);
                            }
                        }
                    }
                }
            }
        }
    }
}

#ifdef TARGET_X86_64
static void tlb_info_la48(Monitor *mon, CPUArchState *env,
        uint64_t l0, uint64_t pml4_addr)
{
    uint64_t l1, l2, l3, l4;
    uint64_t pml4e, pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;

    for (l1 = 0; l1 < 512; l1++) {
        cpu_physical_memory_read(pml4_addr + l1 * 8, &pml4e, 8);
        pml4e = le64_to_cpu(pml4e);
        if (!(pml4e & PG_PRESENT_MASK)) {
            continue;
        }

        pdp_addr = pml4e & 0x3fffffffff000ULL;
        for (l2 = 0; l2 < 512; l2++) {
            cpu_physical_memory_read(pdp_addr + l2 * 8, &pdpe, 8);
            pdpe = le64_to_cpu(pdpe);
            if (!(pdpe & PG_PRESENT_MASK)) {
                continue;
            }

            if (pdpe & PG_PSE_MASK) {
                /* 1G pages, CR4.PSE is ignored */
                print_pte(mon, env, (l0 << 48) + (l1 << 39) + (l2 << 30),
                        pdpe, 0x3ffffc0000000ULL);
                continue;
            }

            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l3 = 0; l3 < 512; l3++) {
                cpu_physical_memory_read(pd_addr + l3 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                if (!(pde & PG_PRESENT_MASK)) {
                    continue;
                }

                if (pde & PG_PSE_MASK) {
                    /* 2M pages, CR4.PSE is ignored */
                    print_pte(mon, env, (l0 << 48) + (l1 << 39) + (l2 << 30) +
                            (l3 << 21), pde, 0x3ffffffe00000ULL);
                    continue;
                }

                pt_addr = pde & 0x3fffffffff000ULL;
                for (l4 = 0; l4 < 512; l4++) {
                    cpu_physical_memory_read(pt_addr
                            + l4 * 8,
                            &pte, 8);
                    pte = le64_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        print_pte(mon, env, (l0 << 48) + (l1 << 39) +
                                (l2 << 30) + (l3 << 21) + (l4 << 12),
                                pte & ~PG_PSE_MASK, 0x3fffffffff000ULL);
                    }
                }
            }
        }
    }
}

static void tlb_info_la57(Monitor *mon, CPUArchState *env)
{
    uint64_t l0;
    uint64_t pml5e;
    uint64_t pml5_addr;

    pml5_addr = env->cr[3] & 0x3fffffffff000ULL;
    for (l0 = 0; l0 < 512; l0++) {
        cpu_physical_memory_read(pml5_addr + l0 * 8, &pml5e, 8);
        pml5e = le64_to_cpu(pml5e);
        if (pml5e & PG_PRESENT_MASK) {
            tlb_info_la48(mon, env, l0, pml5e & 0x3fffffffff000ULL);
        }
    }
}
#endif /* TARGET_X86_64 */

void x86_dump_mmu(Monitor *mon, CPUX86State *env)
{
    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            if (env->cr[4] & CR4_LA57_MASK) {
                tlb_info_la57(mon, env);
            } else {
                tlb_info_la48(mon, env, 0, env->cr[3] & 0x3fffffffff000ULL);
            }
        } else
#endif
        {
            tlb_info_pae32(mon, env);
        }
    } else {
        tlb_info_32(mon, env);
    }
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    x86_dump_mmu(mon, env);
}
