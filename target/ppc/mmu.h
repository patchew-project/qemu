#ifndef MMU_H
#define MMU_H

#ifndef CONFIG_USER_ONLY

/* Common Partition Table Entry Fields */
#define PATBE0_HR                0x8000000000000000
#define PATBE1_GR                0x8000000000000000

/* Partition Table Entry */
struct patb_entry {
    uint64_t patbe0, patbe1;
};

#ifdef TARGET_PPC64

void ppc64_set_external_patb(PowerPCCPU *cpu, void *patb, Error **errp);
bool ppc64_use_proc_tbl(PowerPCCPU *cpu);
bool ppc64_radix_guest(PowerPCCPU *cpu);
int ppc64_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                           int mmu_idx);

#endif /* TARGET_PPC64 */

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_H */
