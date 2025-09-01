/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU helpers for qemu
 *
 * Copyright (c) 2024 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"
#include "system/tcg.h"
#include "cpu.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/target_page.h"
#include "internals.h"
#include "cpu-csr.h"
#include "cpu-mmu.h"
#include "tcg/tcg_loongarch.h"

void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                        uint64_t *dir_width, target_ulong level)
{
    switch (level) {
    case 1:
        *dir_base = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR1_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR1_WIDTH);
        break;
    case 2:
        *dir_base = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR2_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, DIR2_WIDTH);
        break;
    case 3:
        *dir_base = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR3_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR3_WIDTH);
        break;
    case 4:
        *dir_base = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR4_BASE);
        *dir_width = FIELD_EX64(env->CSR_PWCH, CSR_PWCH, DIR4_WIDTH);
        break;
    default:
        /* level may be zero for ldpte */
        *dir_base = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTBASE);
        *dir_width = FIELD_EX64(env->CSR_PWCL, CSR_PWCL, PTWIDTH);
        break;
    }
}

TLBRet loongarch_check_pte(CPULoongArchState *env, MMUContext *context,
                           MMUAccessType access_type, int mmu_idx)
{
    uint64_t plv = mmu_idx;
    uint64_t tlb_entry, tlb_ppn;
    uint8_t tlb_ps, tlb_v, tlb_d, tlb_plv, tlb_nx, tlb_nr, tlb_rplv;

    tlb_entry = context->pte;
    tlb_ps = context->ps;
    tlb_v = FIELD_EX64(tlb_entry, TLBENTRY, V);
    tlb_d = FIELD_EX64(tlb_entry, TLBENTRY, D);
    tlb_plv = FIELD_EX64(tlb_entry, TLBENTRY, PLV);
    if (is_la64(env)) {
        tlb_ppn = FIELD_EX64(tlb_entry, TLBENTRY_64, PPN);
        tlb_nx = FIELD_EX64(tlb_entry, TLBENTRY_64, NX);
        tlb_nr = FIELD_EX64(tlb_entry, TLBENTRY_64, NR);
        tlb_rplv = FIELD_EX64(tlb_entry, TLBENTRY_64, RPLV);
    } else {
        tlb_ppn = FIELD_EX64(tlb_entry, TLBENTRY_32, PPN);
        tlb_nx = 0;
        tlb_nr = 0;
        tlb_rplv = 0;
    }

    /* Remove sw bit between bit12 -- bit PS*/
    tlb_ppn = tlb_ppn & ~(((0x1UL << (tlb_ps - 12)) - 1));

    /* Check access rights */
    if (!tlb_v) {
        return TLBRET_INVALID;
    }

    if (access_type == MMU_INST_FETCH && tlb_nx) {
        return TLBRET_XI;
    }

    if (access_type == MMU_DATA_LOAD && tlb_nr) {
        return TLBRET_RI;
    }

    if (((tlb_rplv == 0) && (plv > tlb_plv)) ||
        ((tlb_rplv == 1) && (plv != tlb_plv))) {
        return TLBRET_PE;
    }

    if ((access_type == MMU_DATA_STORE) && !tlb_d) {
        return TLBRET_DIRTY;
    }

    context->physical = (tlb_ppn << R_TLBENTRY_64_PPN_SHIFT) |
                        (context->addr & MAKE_64BIT_MASK(0, tlb_ps));
    context->prot = PAGE_READ;
    if (tlb_d) {
        context->prot |= PAGE_WRITE;
    }
    if (!tlb_nx) {
        context->prot |= PAGE_EXEC;
    }
    return TLBRET_MATCH;
}

static TLBRet loongarch_page_table_walker(CPULoongArchState *env,
                                          MMUContext *context,
                                          int access_type, int mmu_idx)
{
    CPUState *cs = env_cpu(env);
    target_ulong index, phys;
    uint64_t dir_base, dir_width;
    uint64_t base;
    int level;
    vaddr address;

    address = context->addr;
    if ((address >> 63) & 0x1) {
        base = env->CSR_PGDH;
    } else {
        base = env->CSR_PGDL;
    }
    base &= TARGET_PHYS_MASK;

    for (level = 4; level > 0; level--) {
        get_dir_base_width(env, &dir_base, &dir_width, level);

        if (dir_width == 0) {
            continue;
        }

        /* get next level page directory */
        index = (address >> dir_base) & ((1 << dir_width) - 1);
        phys = base | index << 3;
        base = ldq_phys(cs->as, phys) & TARGET_PHYS_MASK;
        if (FIELD_EX64(base, TLBENTRY, HUGE)) {
            /* base is a huge pte */
            break;
        }
    }

    /* pte */
    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        /* Huge Page. base is pte */
        base = FIELD_DP64(base, TLBENTRY, LEVEL, 0);
        base = FIELD_DP64(base, TLBENTRY, HUGE, 0);
        if (FIELD_EX64(base, TLBENTRY, HGLOBAL)) {
            base = FIELD_DP64(base, TLBENTRY, HGLOBAL, 0);
            base = FIELD_DP64(base, TLBENTRY, G, 1);
        }
    } else {
        /* Normal Page. base points to pte */
        get_dir_base_width(env, &dir_base, &dir_width, 0);
        index = (address >> dir_base) & ((1 << dir_width) - 1);
        phys = base | index << 3;
        base = ldq_phys(cs->as, phys);
    }

    context->ps = dir_base;
    context->pte = base;
    return loongarch_check_pte(env, context, access_type, mmu_idx);
}

static TLBRet loongarch_map_address(CPULoongArchState *env,
                                    MMUContext *context,
                                    MMUAccessType access_type, int mmu_idx,
                                    int is_debug)
{
    TLBRet ret;

    if (tcg_enabled()) {
        ret = loongarch_get_addr_from_tlb(env, context, access_type, mmu_idx);
        if (ret != TLBRET_NOMATCH) {
            return ret;
        }
    }

    if (is_debug) {
        /*
         * For debugger memory access, we want to do the map when there is a
         * legal mapping, even if the mapping is not yet in TLB. return 0 if
         * there is a valid map, else none zero.
         */
        return loongarch_page_table_walker(env, context, access_type, mmu_idx);
    }

    return TLBRET_NOMATCH;
}

static hwaddr dmw_va2pa(CPULoongArchState *env, vaddr va, target_ulong dmw)
{
    if (is_la64(env)) {
        return va & TARGET_VIRT_MASK;
    } else {
        uint32_t pseg = FIELD_EX32(dmw, CSR_DMW_32, PSEG);
        return (va & MAKE_64BIT_MASK(0, R_CSR_DMW_32_VSEG_SHIFT)) | \
            (pseg << R_CSR_DMW_32_VSEG_SHIFT);
    }
}

TLBRet get_physical_address(CPULoongArchState *env, MMUContext *context,
                            MMUAccessType access_type, int mmu_idx,
                            int is_debug)
{
    int user_mode = mmu_idx == MMU_USER_IDX;
    int kernel_mode = mmu_idx == MMU_KERNEL_IDX;
    uint32_t plv, base_c, base_v;
    int64_t addr_high;
    uint8_t da = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, DA);
    uint8_t pg = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PG);
    vaddr address;

    /* Check PG and DA */
    address = context->addr;
    if (da & !pg) {
        context->physical = address & TARGET_PHYS_MASK;
        context->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TLBRET_MATCH;
    }

    plv = kernel_mode | (user_mode << R_CSR_DMW_PLV3_SHIFT);
    if (is_la64(env)) {
        base_v = address >> R_CSR_DMW_64_VSEG_SHIFT;
    } else {
        base_v = address >> R_CSR_DMW_32_VSEG_SHIFT;
    }
    /* Check direct map window */
    for (int i = 0; i < 4; i++) {
        if (is_la64(env)) {
            base_c = FIELD_EX64(env->CSR_DMW[i], CSR_DMW_64, VSEG);
        } else {
            base_c = FIELD_EX64(env->CSR_DMW[i], CSR_DMW_32, VSEG);
        }
        if ((plv & env->CSR_DMW[i]) && (base_c == base_v)) {
            context->physical = dmw_va2pa(env, address, env->CSR_DMW[i]);
            context->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            return TLBRET_MATCH;
        }
    }

    /* Check valid extension */
    addr_high = (int64_t)address >> (TARGET_VIRT_ADDR_SPACE_BITS - 1);
    if (!(addr_high == 0 || addr_high == -1ULL)) {
        return TLBRET_BADADDR;
    }

    /* Mapped address */
    return loongarch_map_address(env, context, access_type, mmu_idx, is_debug);
}

hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    CPULoongArchState *env = cpu_env(cs);
    MMUContext context;

    context.addr = addr;
    if (get_physical_address(env, &context, MMU_DATA_LOAD,
                             cpu_mmu_index(cs, false), 1) != TLBRET_MATCH) {
        return -1;
    }
    return context.physical;
}
