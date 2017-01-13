#ifndef MMU_HASH64_H
#define MMU_HASH64_H

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64
void dump_slb(FILE *f, fprintf_function cpu_fprintf, PowerPCCPU *cpu);
int ppc_store_slb(PowerPCCPU *cpu, target_ulong slot,
                  target_ulong esid, target_ulong vsid);
hwaddr ppc_hash64_get_phys_page_debug(PowerPCCPU *cpu, target_ulong addr);
int ppc_hash64_handle_mmu_fault(PowerPCCPU *cpu, vaddr address, int rw,
                                int mmu_idx);
void ppc_hash64_store_hpte(PowerPCCPU *cpu, target_ulong index,
                           target_ulong pte0, target_ulong pte1);
void ppc_hash64_tlb_flush_hpte(PowerPCCPU *cpu,
                               target_ulong pte_index,
                               target_ulong pte0, target_ulong pte1);
unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1);
void ppc_hash64_update_vrma(CPUPPCState *env);
void ppc_hash64_update_rmls(CPUPPCState *env);
#endif

/*
 * SLB definitions
 */

/* Bits in the SLB ESID word */
#define SLB_ESID_ESID           0xFFFFFFFFF0000000ULL
#define SLB_ESID_V              0x0000000008000000ULL /* valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT          12
#define SLB_VSID_SHIFT_1T       24
#define SLB_VSID_SSIZE_SHIFT    62
#define SLB_VSID_B              0xc000000000000000ULL
#define SLB_VSID_B_256M         0x0000000000000000ULL
#define SLB_VSID_B_1T           0x4000000000000000ULL
#define SLB_VSID_VSID           0x3FFFFFFFFFFFF000ULL
#define SLB_VSID_VRMA           (0x0001FFFFFF000000ULL | SLB_VSID_B_1T)
#define SLB_VSID_PTEM           (SLB_VSID_B | SLB_VSID_VSID)
#define SLB_VSID_KS             0x0000000000000800ULL
#define SLB_VSID_KP             0x0000000000000400ULL
#define SLB_VSID_N              0x0000000000000200ULL /* no-execute */
#define SLB_VSID_L              0x0000000000000100ULL
#define SLB_VSID_C              0x0000000000000080ULL /* class */
#define SLB_VSID_LP             0x0000000000000030ULL
#define SLB_VSID_ATTR           0x0000000000000FFFULL
#define SLB_VSID_LLP_MASK       (SLB_VSID_L | SLB_VSID_LP)
#define SLB_VSID_4K             0x0000000000000000ULL
#define SLB_VSID_64K            0x0000000000000110ULL
#define SLB_VSID_16M            0x0000000000000100ULL
#define SLB_VSID_16G            0x0000000000000120ULL

/*
 * Hash page table definitions
 */

#define HPTES_PER_GROUP         8
#define HASH_PTE_SIZE_64        16
#define HASH_PTEG_SIZE_64       (HASH_PTE_SIZE_64 * HPTES_PER_GROUP)

#define HPTE64_V_3_00_COMMON    0x000fffffffffffffULL
#define HPTE64_V_SSIZE_SHIFT    62
#define HPTE64_V_AVPN_SHIFT     7
#define HPTE64_V_AVPN           0x3fffffffffffff80ULL
#define HPTE64_V_AVPN_VAL(x)    (((x) & HPTE64_V_AVPN) >> HPTE64_V_AVPN_SHIFT)
#define HPTE64_V_COMPARE(x, y)  (!(((x) ^ (y)) & 0xffffffffffffff83ULL))
#define HPTE64_V_LARGE          0x0000000000000004ULL
#define HPTE64_V_SECONDARY      0x0000000000000002ULL
#define HPTE64_V_VALID          0x0000000000000001ULL

#define HPTE64_R_3_00_COMMON    0xf1ffffffffffffffULL
#define HPTE64_R_PP0            0x8000000000000000ULL
#define HPTE64_R_TS             0x4000000000000000ULL
#define HPTE64_R_KEY_HI         0x3000000000000000ULL
#define HPTE64_R_SSIZE_SHIFT    58
#define HPTE64_R_SSIZE_MASK     (3ULL << HPTE64_R_SSIZE_SHIFT)
#define HPTE64_R_RPN_SHIFT      12
#define HPTE64_R_RPN            0x0ffffffffffff000ULL
#define HPTE64_R_FLAGS          0x00000000000003ffULL
#define HPTE64_R_PP             0x0000000000000003ULL
#define HPTE64_R_N              0x0000000000000004ULL
#define HPTE64_R_G              0x0000000000000008ULL
#define HPTE64_R_M              0x0000000000000010ULL
#define HPTE64_R_I              0x0000000000000020ULL
#define HPTE64_R_W              0x0000000000000040ULL
#define HPTE64_R_WIMG           0x0000000000000078ULL
#define HPTE64_R_C              0x0000000000000080ULL
#define HPTE64_R_R              0x0000000000000100ULL
#define HPTE64_R_KEY_LO         0x0000000000000e00ULL
#define HPTE64_R_KEY(x)         ((((x) & HPTE64_R_KEY_HI) >> 60) | \
                                 (((x) & HPTE64_R_KEY_LO) >> 9))

#define HPTE64_V_1TB_SEG        0x4000000000000000ULL
#define HPTE64_V_VRMA_MASK      0x4001ffffff000000ULL

typedef struct {
    uint64_t pte0, pte1;
} ppc_hash_pte64_t;

void ppc_hash64_set_sdr1(PowerPCCPU *cpu, target_ulong value,
                         Error **errp);
void ppc_hash64_set_external_hpt(PowerPCCPU *cpu, void *hpt, int shift,
                                 Error **errp);

uint64_t ppc_hash64_start_access(PowerPCCPU *cpu, target_ulong pte_index);
void ppc_hash64_stop_access(PowerPCCPU *cpu, uint64_t token);

static inline void ppc_hash64_hpte_old_to_new(CPUPPCState *env,
                                              target_ulong *pte0,
                                              target_ulong *pte1)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_3_00:
        /*
         * v3.00 of the ISA moved the B field to the second doubleword and
         * shortened the abbreviated virtual address and abbreviated real page
         * number fields
         */
        *pte1 = (*pte1 & HPTE64_R_3_00_COMMON) |
                ((*pte0 >> HPTE64_V_SSIZE_SHIFT) << HPTE64_R_SSIZE_SHIFT);
        *pte0 = *pte0 & HPTE64_V_3_00_COMMON;
    default:
        ;
    }
}

static inline void ppc_hash64_hpte_new_to_old(CPUPPCState *env,
                                              target_ulong *pte0,
                                              target_ulong *pte1)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_3_00:
        /*
         * v3.00 of the ISA moved the B field to the second doubleword and
         * shortened the abbreviated virtual address and abbreviated real page
         * number fields
         */
        *pte0 = (*pte0 & HPTE64_V_3_00_COMMON) | ((*pte1 & HPTE64_R_SSIZE_MASK)
                << (HPTE64_V_SSIZE_SHIFT - HPTE64_R_SSIZE_SHIFT));
        *pte1 = *pte1 & HPTE64_R_3_00_COMMON;
    default:
        ;
    }
}

static inline ppc_hash_pte64_t ppc_hash64_load_hpte(PowerPCCPU *cpu,
                                                    uint64_t token,
                                                    int index)
{
    CPUPPCState *env = &cpu->env;
    ppc_hash_pte64_t pte;
    uint64_t addr;

    addr = token + (index * HASH_PTE_SIZE_64);
    if (env->external_htab) {
        pte.pte0 = ldq_p((const void *)(uintptr_t)addr);
        pte.pte1 = ldq_p((const void *)(uintptr_t)addr + HASH_PTE_SIZE_64/2);
    } else {
        pte.pte0 = ldq_phys(CPU(cpu)->as, addr);
        pte.pte1 = ldq_phys(CPU(cpu)->as, addr + HASH_PTE_SIZE_64/2);
    }

    ppc_hash64_hpte_new_to_old(env, &pte.pte0, &pte.pte1);
    return pte;
}

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_HASH64_H */
