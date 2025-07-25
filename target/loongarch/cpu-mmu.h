/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU parameters for QEMU.
 *
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_MMU_H
#define LOONGARCH_CPU_MMU_H

enum {
    TLBRET_MATCH = 0,
    TLBRET_BADADDR = 1,
    TLBRET_NOMATCH = 2,
    TLBRET_INVALID = 3,
    TLBRET_DIRTY = 4,
    TLBRET_RI = 5,
    TLBRET_XI = 6,
    TLBRET_PE = 7,
};

typedef struct mmu_context {
    target_ulong  vaddr;
    uint64_t      pte;
    hwaddr        physical;
    int           ps;  /* page size shift */
    int           prot;
    int           tlb_index;
    int           mmu_index;
} mmu_context;

bool check_ps(CPULoongArchState *ent, uint8_t ps);
int loongarch_check_pte(CPULoongArchState *env, mmu_context *context,
                        int access_type, int mmu_idx);
int get_physical_address(CPULoongArchState *env, mmu_context *context,
                         MMUAccessType access_type, int mmu_idx, int is_debug);
void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                               uint64_t *dir_width, target_ulong level);
hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif  /* LOONGARCH_CPU_MMU_H */
