#include "qemu/osdep.h"
#include "cpu.h"
#include "mmu-hash64.h"
#include "fpu/softfloat-helpers.h"
#include "mmu-misc.h"

void ppc_store_lpcr(PowerPCCPU *cpu, target_ulong val)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    env->spr[SPR_LPCR] = val & pcc->lpcr_mask;
}

void ppc_hash64_filter_pagesizes(PowerPCCPU *cpu,
                                 bool (*cb)(void *, uint32_t, uint32_t),
                                 void *opaque)
{
    PPCHash64Options *opts = cpu->hash64_opts;
    int i;
    int n = 0;
    bool ci_largepage = false;

    assert(opts);

    n = 0;
    for (i = 0; i < ARRAY_SIZE(opts->sps); i++) {
        PPCHash64SegmentPageSizes *sps = &opts->sps[i];
        int j;
        int m = 0;

        assert(n <= i);

        if (!sps->page_shift) {
            break;
        }

        for (j = 0; j < ARRAY_SIZE(sps->enc); j++) {
            PPCHash64PageSize *ps = &sps->enc[j];

            assert(m <= j);
            if (!ps->page_shift) {
                break;
            }

            if (cb(opaque, sps->page_shift, ps->page_shift)) {
                if (ps->page_shift >= 16) {
                    ci_largepage = true;
                }
                sps->enc[m++] = *ps;
            }
        }

        /* Clear rest of the row */
        for (j = m; j < ARRAY_SIZE(sps->enc); j++) {
            memset(&sps->enc[j], 0, sizeof(sps->enc[j]));
        }

        if (m) {
            n++;
        }
    }

    /* Clear the rest of the table */
    for (i = n; i < ARRAY_SIZE(opts->sps); i++) {
        memset(&opts->sps[i], 0, sizeof(opts->sps[i]));
    }

    if (!ci_largepage) {
        opts->flags &= ~PPC_HASH64_CI_LARGEPAGE;
    }
}

void ppc_hash64_unmap_hptes(PowerPCCPU *cpu, const ppc_hash_pte64_t *hptes,
                            hwaddr ptex, int n)
{
    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->unmap_hptes(cpu->vhyp, hptes, ptex, n);
        return;
    }

    address_space_unmap(CPU(cpu)->as, (void *)hptes, n * HASH_PTE_SIZE_64,
                        false, n * HASH_PTE_SIZE_64);
}
