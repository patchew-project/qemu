/*
 * QEMU monitor for m68k
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/hmp-target.h"
#include "monitor/monitor.h"

static void print_address_zone(Monitor *mon,
                               uint32_t logical, uint32_t physical,
                               uint32_t size, int attr)
{
    monitor_printf(mon, "%08x - %08x -> %08x - %08x %c ",
                   logical, logical + size - 1,
                   physical, physical + size - 1,
                   attr & 4 ? 'W' : '-');
    size >>= 10;
    if (size < 1024) {
        monitor_printf(mon, "(%d KiB)\n", size);
    } else {
        size >>= 10;
        if (size < 1024) {
            monitor_printf(mon, "(%d MiB)\n", size);
        } else {
            size >>= 10;
            monitor_printf(mon, "(%d GiB)\n", size);
        }
    }
}

static void dump_address_map(Monitor *mon, CPUM68KState *env,
                             uint32_t root_pointer)
{
    int i, j, k;
    int tic_size, tic_shift;
    uint32_t tib_mask;
    uint32_t tia, tib, tic;
    uint32_t logical = 0xffffffff, physical = 0xffffffff;
    uint32_t first_logical = 0xffffffff, first_physical = 0xffffffff;
    uint32_t last_logical, last_physical;
    int32_t size;
    int last_attr = -1, attr = -1;
    CPUState *cs = env_cpu(env);
    MemTxResult txres;

    if (env->mmu.tcr & M68K_TCR_PAGE_8K) {
        /* 8k page */
        tic_size = 32;
        tic_shift = 13;
        tib_mask = M68K_8K_PAGE_MASK;
    } else {
        /* 4k page */
        tic_size = 64;
        tic_shift = 12;
        tib_mask = M68K_4K_PAGE_MASK;
    }
    for (i = 0; i < M68K_ROOT_POINTER_ENTRIES; i++) {
        tia = address_space_ldl(cs->as, M68K_POINTER_BASE(root_pointer) + i * 4,
                                MEMTXATTRS_UNSPECIFIED, &txres);
        if (txres != MEMTX_OK || !M68K_UDT_VALID(tia)) {
            continue;
        }
        for (j = 0; j < M68K_ROOT_POINTER_ENTRIES; j++) {
            tib = address_space_ldl(cs->as, M68K_POINTER_BASE(tia) + j * 4,
                                    MEMTXATTRS_UNSPECIFIED, &txres);
            if (txres != MEMTX_OK || !M68K_UDT_VALID(tib)) {
                continue;
            }
            for (k = 0; k < tic_size; k++) {
                tic = address_space_ldl(cs->as, (tib & tib_mask) + k * 4,
                                        MEMTXATTRS_UNSPECIFIED, &txres);
                if (txres != MEMTX_OK || !M68K_PDT_VALID(tic)) {
                    continue;
                }
                if (M68K_PDT_INDIRECT(tic)) {
                    tic = address_space_ldl(cs->as, M68K_INDIRECT_POINTER(tic),
                                            MEMTXATTRS_UNSPECIFIED, &txres);
                    if (txres != MEMTX_OK) {
                        continue;
                    }
                }

                last_logical = logical;
                logical = (i << M68K_TTS_ROOT_SHIFT) |
                          (j << M68K_TTS_POINTER_SHIFT) |
                          (k << tic_shift);

                last_physical = physical;
                physical = tic & ~((1 << tic_shift) - 1);

                last_attr = attr;
                attr = tic & ((1 << tic_shift) - 1);

                if ((logical != (last_logical + (1 << tic_shift))) ||
                    (physical != (last_physical + (1 << tic_shift))) ||
                    (attr & 4) != (last_attr & 4)) {

                    if (first_logical != 0xffffffff) {
                        size = last_logical + (1 << tic_shift) -
                               first_logical;
                        print_address_zone(mon, first_logical,
                                           first_physical, size, last_attr);
                    }
                    first_logical = logical;
                    first_physical = physical;
                }
            }
        }
    }
    if (first_logical != logical || (attr & 4) != (last_attr & 4)) {
        size = logical + (1 << tic_shift) - first_logical;
        print_address_zone(mon, first_logical, first_physical, size, last_attr);
    }
}

#define DUMP_CACHEFLAGS(a) \
    switch (a & M68K_DESC_CACHEMODE) { \
    case M68K_DESC_CM_WRTHRU: /* cacheable, write-through */ \
        monitor_puts(mon, "T"); \
        break; \
    case M68K_DESC_CM_COPYBK: /* cacheable, copyback */ \
        monitor_puts(mon, "C"); \
        break; \
    case M68K_DESC_CM_SERIAL: /* noncachable, serialized */ \
        monitor_puts(mon, "S"); \
        break; \
    case M68K_DESC_CM_NCACHE: /* noncachable */ \
        monitor_puts(mon, "N"); \
        break; \
    }

static void dump_ttr(Monitor *mon, const char *desc, uint32_t ttr)
{
    monitor_printf(mon, "%s: ", desc);
    if ((ttr & M68K_TTR_ENABLED) == 0) {
        monitor_puts(mon, "disabled\n");
        return;
    }
    monitor_printf(mon, "Base: 0x%08x Mask: 0x%08x Control: ",
                   ttr & M68K_TTR_ADDR_BASE,
                   (ttr & M68K_TTR_ADDR_MASK) << M68K_TTR_ADDR_MASK_SHIFT);
    switch (ttr & M68K_TTR_SFIELD) {
    case M68K_TTR_SFIELD_USER:
        monitor_puts(mon, "U");
        break;
    case M68K_TTR_SFIELD_SUPER:
        monitor_puts(mon, "S");
        break;
    default:
        monitor_puts(mon, "*");
        break;
    }
    DUMP_CACHEFLAGS(ttr);
    if (ttr & M68K_DESC_WRITEPROT) {
        monitor_puts(mon, "R");
    } else {
        monitor_puts(mon, "W");
    }
    monitor_printf(mon, " U: %d\n", (ttr & M68K_DESC_USERATTR) >>
                               M68K_DESC_USERATTR_SHIFT);
}

void m68k_dump_mmu(Monitor *mon, CPUM68KState *env)
{
    if ((env->mmu.tcr & M68K_TCR_ENABLED) == 0) {
        monitor_puts(mon, "Translation disabled\n");
        return;
    }
    monitor_puts(mon, "Page Size: ");
    if (env->mmu.tcr & M68K_TCR_PAGE_8K) {
        monitor_puts(mon, "8kB\n");
    } else {
        monitor_puts(mon, "4kB\n");
    }

    monitor_puts(mon, "MMUSR: ");
    if (env->mmu.mmusr & M68K_MMU_B_040) {
        monitor_puts(mon, "BUS ERROR\n");
    } else {
        monitor_printf(mon, "Phy=%08x Flags: ", env->mmu.mmusr & 0xfffff000);
        /* flags found on the page descriptor */
        if (env->mmu.mmusr & M68K_MMU_G_040) {
            monitor_puts(mon, "G"); /* Global */
        } else {
            monitor_puts(mon, ".");
        }
        if (env->mmu.mmusr & M68K_MMU_S_040) {
            monitor_puts(mon, "S"); /* Supervisor */
        } else {
            monitor_puts(mon, ".");
        }
        if (env->mmu.mmusr & M68K_MMU_M_040) {
            monitor_puts(mon, "M"); /* Modified */
        } else {
            monitor_puts(mon, ".");
        }
        if (env->mmu.mmusr & M68K_MMU_WP_040) {
            monitor_puts(mon, "W"); /* Write protect */
        } else {
            monitor_puts(mon, ".");
        }
        if (env->mmu.mmusr & M68K_MMU_T_040) {
            monitor_puts(mon, "T"); /* Transparent */
        } else {
            monitor_puts(mon, ".");
        }
        if (env->mmu.mmusr & M68K_MMU_R_040) {
            monitor_puts(mon, "R"); /* Resident */
        } else {
            monitor_puts(mon, ".");
        }
        monitor_puts(mon, " Cache: ");
        DUMP_CACHEFLAGS(env->mmu.mmusr);
        monitor_printf(mon, " U: %d\n", (env->mmu.mmusr >> 8) & 3);
        monitor_puts(mon, "\n");
    }

    dump_ttr(mon, "ITTR0", env->mmu.ttr[M68K_ITTR0]);
    dump_ttr(mon, "ITTR1", env->mmu.ttr[M68K_ITTR1]);
    dump_ttr(mon, "DTTR0", env->mmu.ttr[M68K_DTTR0]);
    dump_ttr(mon, "DTTR1", env->mmu.ttr[M68K_DTTR1]);

    monitor_printf(mon, "SRP: 0x%08x\n", env->mmu.srp);
    dump_address_map(mon, env, env->mmu.srp);

    monitor_printf(mon, "URP: 0x%08x\n", env->mmu.urp);
    dump_address_map(mon, env, env->mmu.urp);
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env(mon);

    if (!env1) {
        monitor_puts(mon, "No CPU available\n");
        return;
    }

    m68k_dump_mmu(mon, env1);
}

static const MonitorDef monitor_defs[] = {
    { "d0", offsetof(CPUM68KState, dregs[0]) },
    { "d1", offsetof(CPUM68KState, dregs[1]) },
    { "d2", offsetof(CPUM68KState, dregs[2]) },
    { "d3", offsetof(CPUM68KState, dregs[3]) },
    { "d4", offsetof(CPUM68KState, dregs[4]) },
    { "d5", offsetof(CPUM68KState, dregs[5]) },
    { "d6", offsetof(CPUM68KState, dregs[6]) },
    { "d7", offsetof(CPUM68KState, dregs[7]) },
    { "a0", offsetof(CPUM68KState, aregs[0]) },
    { "a1", offsetof(CPUM68KState, aregs[1]) },
    { "a2", offsetof(CPUM68KState, aregs[2]) },
    { "a3", offsetof(CPUM68KState, aregs[3]) },
    { "a4", offsetof(CPUM68KState, aregs[4]) },
    { "a5", offsetof(CPUM68KState, aregs[5]) },
    { "a6", offsetof(CPUM68KState, aregs[6]) },
    { "a7", offsetof(CPUM68KState, aregs[7]) },
    { "pc", offsetof(CPUM68KState, pc) },
    { "sr", offsetof(CPUM68KState, sr) },
    { "ssp", offsetof(CPUM68KState, sp[0]) },
    { "usp", offsetof(CPUM68KState, sp[1]) },
    { "isp", offsetof(CPUM68KState, sp[2]) },
    { "sfc", offsetof(CPUM68KState, sfc) },
    { "dfc", offsetof(CPUM68KState, dfc) },
    { "urp", offsetof(CPUM68KState, mmu.urp) },
    { "srp", offsetof(CPUM68KState, mmu.srp) },
    { "dttr0", offsetof(CPUM68KState, mmu.ttr[M68K_DTTR0]) },
    { "dttr1", offsetof(CPUM68KState, mmu.ttr[M68K_DTTR1]) },
    { "ittr0", offsetof(CPUM68KState, mmu.ttr[M68K_ITTR0]) },
    { "ittr1", offsetof(CPUM68KState, mmu.ttr[M68K_ITTR1]) },
    { "mmusr", offsetof(CPUM68KState, mmu.mmusr) },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}
