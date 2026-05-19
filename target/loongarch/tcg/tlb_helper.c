/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch TLB helpers
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"

#include "cpu.h"
#include "cpu-mmu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/log.h"
#include "cpu-csr.h"
#include "tcg/tcg_loongarch.h"
#include "system/memory.h"

typedef bool (*tlb_match)(bool global, int asid, int tlb_asid);

static bool tlb_match_any(bool global, int asid, int tlb_asid)
{
    return global || tlb_asid == asid;
}

static bool tlb_match_asid(bool global, int asid, int tlb_asid)
{
    return !global && tlb_asid == asid;
}

static inline bool tlb_entry_matches_gid(LoongArchTLB *tlb, uint8_t gid)
{
    uint8_t entry_gid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, GID);

    return entry_gid == gid;
}

bool check_ps(CPULoongArchState *env, uint8_t tlb_ps)
{
    if (tlb_ps >= 64) {
        return false;
    }
    return BIT_ULL(tlb_ps) & (env->CSR_PRCFG2);
}

static void raise_mmu_exception(CPULoongArchState *env, vaddr address,
                                MMUAccessType access_type, TLBRet tlb_error)
{
    CPUState *cs = env_cpu(env);
    bool real_guest;

    if (env->guest && tlb_error > TLBRET_HOST_MATCH) {
        trigger_vm_exit(env);
    }
    real_guest = !env->vm_exit && env->guest;

    switch (tlb_error) {
    default:
    case TLBRET_BADADDR:
    case TLBRET_HOST_BADADDR:
        cs->exception_index = access_type == MMU_INST_FETCH
                              ? EXCCODE_ADEF : EXCCODE_ADEM;
        break;
    case TLBRET_NOMATCH:
    case TLBRET_HOST_NOMATCH:
        /* No TLB match for a mapped address */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        SET_CSR_IF(real_guest, TLBRERA,
                   FIELD_DP64(GET_CSR_IF(real_guest, TLBRERA), CSR_TLBRERA,
                              ISTLBR, 1));
        break;
    case TLBRET_INVALID:
    case TLBRET_HOST_INVALID:
        /* TLB match with no valid bit */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        break;
    case TLBRET_DIRTY:
    case TLBRET_HOST_DIRTY:
        /* TLB match but 'D' bit is cleared */
        cs->exception_index = EXCCODE_PME;
        break;
    case TLBRET_XI:
    case TLBRET_HOST_XI:
        /* Execute-Inhibit Exception */
        cs->exception_index = EXCCODE_PNX;
        break;
    case TLBRET_RI:
    case TLBRET_HOST_RI:
        /* Read-Inhibit Exception */
        cs->exception_index = EXCCODE_PNR;
        break;
    case TLBRET_PE:
    case TLBRET_HOST_PE:
        /* Privileged Exception */
        cs->exception_index = EXCCODE_PPI;
        break;
    }

    if (tlb_error == TLBRET_NOMATCH || tlb_error == TLBRET_HOST_NOMATCH) {
        SET_CSR_IF(real_guest, TLBRBADV, address);
        if (is_la64(env)) {
            SET_CSR_IF(real_guest, TLBREHI,
                       FIELD_DP64(GET_CSR_IF(real_guest, TLBREHI),
                                  CSR_TLBREHI_64, VPPN,
                                  extract64(address, 13, 35)));
        } else {
            SET_CSR_IF(real_guest, TLBREHI,
                       FIELD_DP64(GET_CSR_IF(real_guest, TLBREHI),
                                  CSR_TLBREHI_32, VPPN,
                                  extract64(address, 13, 19)));
        }
    } else {
        if (!FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            SET_CSR_IF(real_guest, BADV, address);
        }
        SET_CSR_IF(real_guest, TLBEHI, address & (TARGET_PAGE_MASK << 1));
    }
}

static void invalidate_tlb_entry(CPULoongArchState *env, int index, bool guest)
{
    target_ulong addr, mask, pagesize;
    uint8_t tlb_ps;
    LoongArchTLB *tlb = guest ? &env->gtlb[index] : &env->tlb[index];
    int idxmap =
        guest ? (BIT(MMU_GUEST_IDX) | BIT(MMU_GUEST_IDX + 1) |
                 BIT(MMU_GUEST_IDX + 2) | BIT(MMU_GUEST_IDX + 3) |
                 BIT(MMU_GUEST_DA_IDX)) :
                (BIT(MMU_KERNEL_IDX) | BIT(MMU_USER_IDX) | BIT(MMU_DA_IDX));
    uint64_t tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
    bool tlb_v;

    tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    pagesize = MAKE_64BIT_MASK(tlb_ps, 1);
    mask = MAKE_64BIT_MASK(0, tlb_ps + 1);
    addr = (tlb_vppn << R_TLB_MISC_VPPN_SHIFT) & ~mask;
    addr = sextract64(addr, 0, TARGET_VIRT_ADDR_SPACE_BITS);

    tlb_v = pte_present(env, tlb->tlb_entry0, guest);
    if (tlb_v) {
        tlb_flush_range_by_mmuidx(env_cpu(env), addr, pagesize,
                                  idxmap, TARGET_LONG_BITS);
    }

    tlb_v = pte_present(env, tlb->tlb_entry1, guest);
    if (tlb_v) {
        tlb_flush_range_by_mmuidx(env_cpu(env), addr + pagesize, pagesize,
                                  idxmap, TARGET_LONG_BITS);
    }
}

static void invalidate_tlb(CPULoongArchState *env, int index, bool guest)
{
    LoongArchTLB *tlb;
    uint16_t csr_asid, tlb_asid, tlb_g;
    uint8_t tlb_e;

    csr_asid = FIELD_EX64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID);
    tlb = guest ? &env->gtlb[index] : &env->tlb[index];
    tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
    if (!tlb_e) {
        return;
    }

    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
    tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
    tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
    /* QEMU TLB is flushed when asid is changed */
    if (tlb_g == 0 && tlb_asid != csr_asid) {
        return;
    }
    invalidate_tlb_entry(env, index, guest);
}

/* Prepare tlb entry information in software PTW mode */
static void sptw_prepare_context(CPULoongArchState *env, MMUContext *context,
                                 bool guest)
{
    uint64_t lo0, lo1, csr_vppn;
    uint8_t csr_ps;

    if (FIELD_EX64(GET_CSR_IF(guest, TLBRERA), CSR_TLBRERA, ISTLBR)) {
        csr_ps = FIELD_EX64(GET_CSR_IF(guest, TLBREHI), CSR_TLBREHI, PS);
        if (is_la64(env)) {
            csr_vppn =
                FIELD_EX64(GET_CSR_IF(guest, TLBREHI), CSR_TLBREHI_64, VPPN);
        } else {
            csr_vppn =
                FIELD_EX64(GET_CSR_IF(guest, TLBREHI), CSR_TLBREHI_32, VPPN);
        }
        lo0 = GET_CSR_IF(guest, TLBRELO0);
        lo1 = GET_CSR_IF(guest, TLBRELO1);
    } else {
        csr_ps = FIELD_EX64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, PS);
        if (is_la64(env)) {
            csr_vppn =
                FIELD_EX64(GET_CSR_IF(guest, TLBEHI), CSR_TLBEHI_64, VPPN);
        } else {
            csr_vppn =
                FIELD_EX64(GET_CSR_IF(guest, TLBEHI), CSR_TLBEHI_32, VPPN);
        }
        lo0 = GET_CSR_IF(guest, TLBELO0);
        lo1 = GET_CSR_IF(guest, TLBELO1);
    }

    context->ps = csr_ps;
    context->addr = csr_vppn << R_TLB_MISC_VPPN_SHIFT;
    context->pte_buddy[0] = lo0;
    context->pte_buddy[1] = lo1;
}

static void fill_tlb_entry(CPULoongArchState *env, LoongArchTLB *tlb,
                           MMUContext *context, bool guest)
{
    uint64_t lo0, lo1, csr_vppn;
    uint16_t csr_asid;
    uint8_t csr_ps;

    csr_vppn = context->addr >> R_TLB_MISC_VPPN_SHIFT;
    csr_ps   = context->ps;
    lo0      = context->pte_buddy[0];
    lo1      = context->pte_buddy[1];

    /* Store page size in field PS */
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, PS, csr_ps);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, VPPN, csr_vppn);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 1);
    csr_asid = FIELD_EX64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, ASID, csr_asid);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, GID, get_tgid(env));

    tlb->tlb_entry0 = lo0;
    tlb->tlb_entry1 = lo1;
}

/* Return an random value between low and high */
static uint32_t get_random_tlb(uint32_t low, uint32_t high)
{
    uint32_t val;

    qemu_guest_getrandom_nofail(&val, sizeof(val));
    return val % (high - low + 1) + low;
}

/*
 * One tlb entry holds an adjacent odd/even pair, the vpn is the
 * content of the virtual page number divided by 2. So the
 * compare vpn is bit[47:15] for 16KiB page. while the vppn
 * field in tlb entry contains bit[47:13], so need adjust.
 * virt_vpn = vaddr[47:13]
 */
static LoongArchTLB *loongarch_tlb_search_cb(CPULoongArchState *env,
                                             vaddr vaddr, int csr_asid,
                                             tlb_match func, bool guest,
                                             uint8_t gid)
{
    LoongArchTLB *tlb;
    uint16_t tlb_asid, stlb_idx;
    uint8_t tlb_e, tlb_ps, stlb_ps;
    bool tlb_g;
    int i, compare_shift;
    uint64_t vpn, tlb_vppn;

    stlb_ps = FIELD_EX64(GET_CSR_IF(guest, STLBPS), CSR_STLBPS, PS);
    vpn = (vaddr & TARGET_VIRT_MASK) >> (stlb_ps + 1);
    stlb_idx = vpn & 0xff; /* VA[25:15] <==> TLBIDX.index for 16KiB Page */
    compare_shift = stlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;

    /* Search STLB */
    for (i = 0; i < 8; ++i) {
        tlb = guest ? &env->gtlb[i * 256 + stlb_idx] :
                      &env->tlb[i * 256 + stlb_idx];
        tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
        if (tlb_e) {
            tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = !!FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);

            if (func(tlb_g, csr_asid, tlb_asid) &&
                tlb_entry_matches_gid(tlb, gid) &&
                (vpn == (tlb_vppn >> compare_shift))) {
                return tlb;
            }
        }
    }

    /* Search MTLB */
    for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; ++i) {
        tlb = guest ? &env->gtlb[i] : &env->tlb[i];
        tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
        if (tlb_e) {
            tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
            tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            compare_shift = tlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;
            vpn = (vaddr & TARGET_VIRT_MASK) >> (tlb_ps + 1);
            if (func(tlb_g, csr_asid, tlb_asid) &&
                tlb_entry_matches_gid(tlb, gid) &&
                (vpn == (tlb_vppn >> compare_shift))) {
                return tlb;
            }
        }
    }
    return NULL;
}

static bool loongarch_tlb_search(CPULoongArchState *env, vaddr vaddr,
                                 int *index, bool guest, uint8_t gid)
{
    int csr_asid;
    tlb_match func;
    LoongArchTLB *tlb;

    func = tlb_match_any;
    csr_asid = FIELD_EX64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID);
    tlb = loongarch_tlb_search_cb(env, vaddr, csr_asid, func, guest, gid);
    if (tlb) {
        *index = guest ? (tlb - env->gtlb) : (tlb - env->tlb);
        return true;
    }

    return false;
}

void helper_tlbsrch(CPULoongArchState *env)
{
    int index, match;
    vaddr search_ehi;

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBRERA), CSR_TLBRERA, ISTLBR)) {
        search_ehi = GET_CSR_IF(env->guest, TLBREHI);
    } else {
        search_ehi = GET_CSR_IF(env->guest, TLBEHI);
    }

    match = loongarch_tlb_search(env, search_ehi, &index, env->guest,
                                 get_tgid(env));

    if (match) {
        SET_CSR_IF(env->guest, TLBIDX,
                   FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX,
                              index));
        SET_CSR_IF(
            env->guest, TLBIDX,
            FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 0));
        return;
    }

    SET_CSR_IF(env->guest, TLBIDX,
               FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 1));
}

static void read_tlb(CPULoongArchState *env, bool guest)
{
    LoongArchTLB *tlb;
    int index;
    uint8_t tlb_ps, tlb_e;

    index = FIELD_EX64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, INDEX);
    tlb = guest ? &env->gtlb[index] : &env->tlb[index];
    tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);

    if (!tlb_e) {
        /* Invalid TLB entry */
        SET_CSR_IF(guest, TLBIDX,
                   FIELD_DP64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, NE, 1));
        SET_CSR_IF(guest, ASID,
                   FIELD_DP64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID, 0));
        SET_CSR_IF(guest, TLBEHI, 0);
        SET_CSR_IF(guest, TLBELO0, 0);
        SET_CSR_IF(guest, TLBELO1, 0);
        SET_CSR_IF(guest, TLBIDX,
                   FIELD_DP64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, PS, 0));
    } else {
        /* Valid TLB entry */
        SET_CSR_IF(guest, TLBIDX,
                   FIELD_DP64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, NE, 0));
        SET_CSR_IF(guest, TLBIDX,
                   FIELD_DP64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, PS,
                              tlb_ps & 0x3f));
        SET_CSR_IF(guest, TLBEHI,
                   FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN)
                       << R_TLB_MISC_VPPN_SHIFT);
        SET_CSR_IF(guest, TLBELO0, tlb->tlb_entry0);
        SET_CSR_IF(guest, TLBELO1, tlb->tlb_entry1);
    }
}

void helper_tlbrd(CPULoongArchState *env)
{
    read_tlb(env, env->guest);
}

void helper_gtlbsrch(CPULoongArchState *env)
{
    int index, match;
    vaddr search_ehi;

    if (FIELD_EX64(env->GCSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        search_ehi = env->GCSR_TLBREHI;
    } else {
        search_ehi = env->GCSR_TLBEHI;
    }

    match = loongarch_tlb_search(env, search_ehi, &index, true, get_tgid(env));
    if (match) {
        env->GCSR_TLBIDX =
            FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX, index);
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 0);
        return;
    }
    env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 1);
}

void helper_gtlbrd(CPULoongArchState *env)
{
    read_tlb(env, true);
}

static void update_tlb_index(CPULoongArchState *env, MMUContext *context,
                             int index, bool guest)
{
    LoongArchTLB *old, new = {};
    bool skip_inv = false, tlb_v0, tlb_v1;

    old = guest ? env->gtlb + index : env->tlb + index;
    fill_tlb_entry(env, &new, context, guest);
    /* Check whether ASID/VPPN is the same */
    if (old->tlb_misc == new.tlb_misc) {
        /* Check whether both even/odd pages is the same or invalid */
        tlb_v0 = pte_present(env, old->tlb_entry0, guest);
        tlb_v1 = pte_present(env, old->tlb_entry1, guest);
        if ((!tlb_v0 || new.tlb_entry0 == old->tlb_entry0) &&
            (!tlb_v1 || new.tlb_entry1 == old->tlb_entry1)) {
            skip_inv = true;
        }
    }

    /* flush tlb before updating the entry */
    if (!skip_inv) {
        invalidate_tlb(env, index, guest);
    }

    *old = new;
}

void helper_tlbwr(CPULoongArchState *env)
{
    int index = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX);
    MMUContext context;

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE)) {
        invalidate_tlb(env, index, env->guest);
        return;
    }

    sptw_prepare_context(env, &context, env->guest);
    update_tlb_index(env, &context, index, env->guest);
}

void helper_gtlbwr(CPULoongArchState *env)
{
    int index = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX);
    MMUContext context;

    if (FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, NE)) {
        invalidate_tlb(env, index, true);
        return;
    }

    sptw_prepare_context(env, &context, true);
    update_tlb_index(env, &context, index, true);
}

static int get_tlb_random_index(CPULoongArchState *env, vaddr addr,
                                int pagesize, bool guest)
{
    uint64_t address;
    int index, set, i, stlb_idx;
    uint16_t asid, tlb_asid, stlb_ps;
    LoongArchTLB *tlb;
    uint8_t tlb_e, tlb_g;

    /* Validity of stlb_ps is checked in helper_csrwr_stlbps() */
    stlb_ps = FIELD_EX64(GET_CSR_IF(guest, STLBPS), CSR_STLBPS, PS);
    asid = FIELD_EX64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID);
    if (pagesize == stlb_ps) {
        /* Only write into STLB bits [47:13] */
        address = addr & ~MAKE_64BIT_MASK(0, R_CSR_TLBEHI_64_VPPN_SHIFT);
        set = -1;
        stlb_idx = (address >> (stlb_ps + 1)) & 0xff; /* [0,255] */
        for (i = 0; i < 8; ++i) {
            tlb = guest ? &env->gtlb[i * 256 + stlb_idx] :
                          &env->tlb[i * 256 + stlb_idx];
            tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);
            if (!tlb_e) {
                set = i;
                break;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (tlb_g == 0 && asid != tlb_asid &&
                tlb_entry_matches_gid(tlb, get_tgid(env))) {
                set = i;
            }
        }

        /* Choose one set randomly */
        if (set < 0) {
            set = get_random_tlb(0, 7);
        }
        index = set * 256 + stlb_idx;
    } else {
        /* Only write into MTLB */
        index = -1;
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = guest ? &env->gtlb[i] : &env->tlb[i];
            tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);

            if (!tlb_e) {
                index = i;
                break;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (tlb_g == 0 && asid != tlb_asid &&
                tlb_entry_matches_gid(tlb, get_tgid(env))) {
                index = i;
            }
        }

        if (index < 0) {
            index = get_random_tlb(LOONGARCH_STLB, LOONGARCH_TLB_MAX - 1);
        }
    }

    return index;
}

void helper_tlbfill(CPULoongArchState *env)
{
    vaddr entryhi;
    int index, pagesize;
    MMUContext context;

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBRERA), CSR_TLBRERA, ISTLBR)) {
        entryhi = GET_CSR_IF(env->guest, TLBREHI);
        /* Validity of pagesize is checked in helper_ldpte() */
        pagesize = FIELD_EX64(GET_CSR_IF(env->guest, TLBREHI), CSR_TLBREHI, PS);
    } else {
        entryhi = GET_CSR_IF(env->guest, TLBEHI);
        /* Validity of pagesize is checked in helper_tlbrd() */
        pagesize = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, PS);
    }

    sptw_prepare_context(env, &context, env->guest);
    index = get_tlb_random_index(env, entryhi, pagesize, env->guest);
    invalidate_tlb(env, index, env->guest);
    fill_tlb_entry(env, env->guest ? env->gtlb + index : env->tlb + index,
                   &context, env->guest);
}

void helper_gtlbfill(CPULoongArchState *env)
{
    vaddr entryhi;
    int index, pagesize;
    MMUContext context;

    if (FIELD_EX64(env->GCSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        entryhi = env->GCSR_TLBREHI;
        pagesize = FIELD_EX64(env->GCSR_TLBREHI, CSR_TLBREHI, PS);
    } else {
        entryhi = env->GCSR_TLBEHI;
        pagesize = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, PS);
    }

    sptw_prepare_context(env, &context, true);
    index = get_tlb_random_index(env, entryhi, pagesize, true);
    invalidate_tlb(env, index, true);
    fill_tlb_entry(env, env->gtlb + index, &context, true);
}

static void clear_tlb_by_index(CPULoongArchState *env, bool guest)
{
    LoongArchTLB *tlb;
    int i, index;
    uint16_t csr_asid, tlb_asid, tlb_g;

    csr_asid = FIELD_EX64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID);
    index = FIELD_EX64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, INDEX);

    if (index < LOONGARCH_STLB) {
        for (i = 0; i < 8; i++) {
            tlb = guest ? &env->gtlb[i * 256 + (index % 256)] :
                          &env->tlb[i * 256 + (index % 256)];
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid &&
                tlb_entry_matches_gid(tlb, get_tgid(env))) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = guest ? &env->gtlb[i] : &env->tlb[i];
            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid &&
                tlb_entry_matches_gid(tlb, get_tgid(env))) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    }

    tlb_flush(env_cpu(env));
}

void helper_tlbclr(CPULoongArchState *env)
{
    clear_tlb_by_index(env, env->guest);
}

void helper_gtlbclr(CPULoongArchState *env)
{
    clear_tlb_by_index(env, true);
}

static void flush_tlb_by_index(CPULoongArchState *env, bool guest)
{
    int i, index;

    index = FIELD_EX64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, INDEX);

    if (index < LOONGARCH_STLB) {
        for (i = 0; i < 8; i++) {
            int s_idx = i * 256 + (index % 256);
            LoongArchTLB *tlb = guest ? &env->gtlb[s_idx] : &env->tlb[s_idx];

            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            LoongArchTLB *tlb = guest ? &env->gtlb[i] : &env->tlb[i];

            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
    }

    tlb_flush(env_cpu(env));
}

void helper_tlbflush(CPULoongArchState *env)
{
    flush_tlb_by_index(env, env->guest);
}

void helper_gtlbflush(CPULoongArchState *env)
{
    flush_tlb_by_index(env, true);
}

void helper_invtlb_all(CPULoongArchState *env, target_ulong info, uint32_t op,
                       uint32_t to_guest)
{
    uint16_t gid = to_guest ? (info & 0xff) : get_tgid(env);

    if (to_guest && env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }

    to_guest |= env->guest;

    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        LoongArchTLB *tlb = &env->tlb[i];
        LoongArchTLB *gtlb = &env->gtlb[i];

        if (!to_guest && (op == 0 || tlb_entry_matches_gid(tlb, 0))) {
            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
        if ((!to_guest && op == 0) ||
            (to_guest && tlb_entry_matches_gid(gtlb, gid))) {
            gtlb->tlb_misc = FIELD_DP64(gtlb->tlb_misc, TLB_MISC, E, 0);
        }
    }
    tlb_flush(env_cpu(env));
}

void helper_invtlb_all_g(CPULoongArchState *env, target_ulong info, uint32_t g,
                         uint32_t to_guest)
{
    uint16_t gid = to_guest ? (info & 0xff) : get_tgid(env);

    if (to_guest && env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }

    to_guest |= env->guest;

    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        LoongArchTLB *tlb = to_guest ? &env->gtlb[i] : &env->tlb[i];
        uint8_t tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);

        if (tlb_g == g && tlb_entry_matches_gid(tlb, gid)) {
            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
    }
    tlb_flush(env_cpu(env));
}

void helper_invtlb_all_asid(CPULoongArchState *env, target_ulong info,
                            uint32_t to_guest)
{
    uint16_t asid = info & R_CSR_ASID_ASID_MASK;
    uint16_t gid = to_guest ? ((info >> 16) & 0xff) : get_tgid(env);

    if (to_guest && env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }

    to_guest |= env->guest;

    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        LoongArchTLB *tlb = to_guest ? &env->gtlb[i] : &env->tlb[i];
        uint8_t tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
        uint16_t tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);

        if (!tlb_g && tlb_asid == asid && tlb_entry_matches_gid(tlb, gid)) {
            tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
        }
    }
    tlb_flush(env_cpu(env));
}

void helper_invtlb_page_asid(CPULoongArchState *env, target_ulong info,
                             target_ulong addr, uint32_t to_guest)
{
    uint16_t asid = info & R_CSR_ASID_ASID_MASK;
    uint16_t gid = to_guest ? ((info >> 16) & 0xff) : get_tgid(env);
    LoongArchTLB *tlb;
    int index;

    if (to_guest && env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }
    to_guest |= env->guest;

    tlb =
        loongarch_tlb_search_cb(env, addr, asid, tlb_match_asid, to_guest, gid);
    if (tlb) {
        index = to_guest ? (tlb - env->gtlb) : (tlb - env->tlb);
        invalidate_tlb(env, index, to_guest);
    }
}

void helper_invtlb_page_asid_or_g(CPULoongArchState *env, target_ulong info,
                                  target_ulong addr, uint32_t to_guest)
{
    uint16_t asid = info & R_CSR_ASID_ASID_MASK;
    uint16_t gid = to_guest ? ((info >> 16) & 0xff) : get_tgid(env);
    LoongArchTLB *tlb;
    int index;

    if (to_guest && env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }

    to_guest |= env->guest;

    tlb =
        loongarch_tlb_search_cb(env, addr, asid, tlb_match_any, to_guest, gid);
    if (tlb) {
        index = to_guest ? (tlb - env->gtlb) : (tlb - env->tlb);
        invalidate_tlb(env, index, to_guest);
    }
}

static void ptw_update_tlb(CPULoongArchState *env, MMUContext *context,
                           bool guest)
{
    int index;

    index = context->tlb_index;
    if (index < 0) {
        index = get_tlb_random_index(env, context->addr, context->ps, guest);
    }

    update_tlb_index(env, context, index, guest);
}

TLBRet loongarch_map_host_address(CPULoongArchState *env, MMUContext *context,
                                  MMUAccessType access_type, uintptr_t retaddr)
{
    TLBRet ret;
    ret = loongarch_map_address(env, context, access_type, MMU_KERNEL_IDX,
                                false, false, retaddr);
    return TLBRET_HOST_MATCH + ret;
}

static void loongarch_try_ptw(CPULoongArchState *env, MMUContext *context,
                              MMUAccessType access_type, int mmu_index,
                              TLBRet *status, bool guest, uintptr_t retaddr)
{
    if ((*status == TLBRET_MATCH || *status == TLBRET_HOST_MATCH) &&
        context->mmu_index != MMU_DA_IDX &&
        context->mmu_index != MMU_GUEST_DA_IDX && cpu_has_ptw(env, guest)) {
        bool need_update = true;

        if (access_type == MMU_DATA_STORE && pte_dirty(context->pte)) {
            need_update = false;
        } else if (access_type != MMU_DATA_STORE && pte_access(context->pte)) {
            need_update = false;

            /*
             * FIXME: should context.prot be set without PAGE_WRITE with
             * pte_write(context.pte) && !pte_dirty(context.pte)??
             *
             * Otherwise there will be no loongarch_cpu_tlb_fill() function call
             * for MMU_DATA_STORE access_type in future since QEMU TLB with
             * prot PAGE_WRITE is added already
             */
        }

        if (need_update) {
            /* Need update bit A/D in PTE entry, take PTW again */
            *status =
                (env->guest && !guest) ? TLBRET_HOST_NOMATCH : TLBRET_NOMATCH;
        }
    }

    if (*status != TLBRET_MATCH && *status != TLBRET_HOST_MATCH &&
        cpu_has_ptw(env, guest)) {
        /* Take HW PTW if TLB missed or bit P is zero */
        if (*status == TLBRET_NOMATCH || *status == TLBRET_INVALID ||
            *status == TLBRET_HOST_NOMATCH || *status == TLBRET_HOST_INVALID) {
            *status =
                ((env->guest && !guest) ? TLBRET_HOST_MATCH : TLBRET_MATCH) +
                loongarch_ptw(env, context, access_type, mmu_index, 0, guest,
                              retaddr);
            if (*status == TLBRET_MATCH || *status == TLBRET_HOST_MATCH) {
                ptw_update_tlb(env, context, guest);
            }
        } else if (context->tlb_index >= 0) {
            invalidate_tlb(env, context->tlb_index, guest);
        }
    }
}

bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx, bool probe,
                            uintptr_t retaddr)
{
    CPULoongArchState *env = cpu_env(cs);
    MMUContext host_context;
    hwaddr physical;
    int prot, host_prot;
    MMUContext context;
    TLBRet ret;

    /* Data access */
    context.addr = address;
    context.tlb_index = -1;
    ret = get_physical_address(env, &context, access_type, mmu_idx, 0, retaddr);
    loongarch_try_ptw(env, &context, access_type, mmu_idx, &ret, env->guest,
                      retaddr);

    if (ret == TLBRET_MATCH) {
        physical = context.physical;
        prot = context.prot;
        if (env->guest) {
            host_context.addr = physical;
            host_context.tlb_index = -1;
            ret = loongarch_map_host_address(env, &host_context, access_type,
                                             retaddr);
            loongarch_try_ptw(env, &host_context, access_type, MMU_KERNEL_IDX,
                              &ret, false, retaddr);
            if (ret != TLBRET_HOST_MATCH) {
                if (probe) {
                    return false;
                }
                raise_mmu_exception(env, physical, access_type, ret);
                cpu_loop_exit_restore(cs, retaddr);
                return false;
            }
            physical = host_context.physical;
            host_prot = host_context.prot;
            prot &= host_prot;
        }
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " physical " HWADDR_FMT_plx
                      " prot %d guest %d\n",
                      __func__, address, physical, prot,
                      is_guest_mmu_idx(mmu_idx));
        return true;
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d\n", __func__, address,
                      ret);
    }
    if (probe) {
        return false;
    }
    raise_mmu_exception(env, address, access_type, ret);
    cpu_loop_exit_restore(cs, retaddr);
}

static inline uint64_t loongarch_sanitize_hw_pte(CPULoongArchState *env,
                                                 uint64_t pte)
{
    uint64_t palen_mask = loongarch_palen_mask(env);
    uint64_t ppn_mask = is_la64(env) ? R_TLBENTRY_64_PPN_MASK : R_TLBENTRY_32_PPN_MASK;

    /*
     * Keep only architecturally-defined PTE bits. Guests may use some
     * otherwise-unused bits for software purposes.
     */
    pte &= env->hw_pte_mask;

    return (pte & ~ppn_mask) | ((pte & ppn_mask) & palen_mask);
}

hwaddr loongarch_get_host_address(CPULoongArchState *env, hwaddr gpa,
                                  uintptr_t retaddr)
{
    MMUContext host_context;
    TLBRet ret;

    if (!env->guest) {
        return gpa;
    }

    host_context.addr = gpa;
    host_context.tlb_index = -1;
    ret =
        loongarch_map_host_address(env, &host_context, MMU_DATA_LOAD, retaddr);
    loongarch_try_ptw(env, &host_context, MMU_DATA_LOAD, MMU_KERNEL_IDX, &ret,
                      false, retaddr);

    if (ret != TLBRET_HOST_MATCH) {
        raise_mmu_exception(env, gpa, MMU_DATA_LOAD, ret);
        cpu_loop_exit_restore(env_cpu(env), retaddr);
    }

    return host_context.physical;
}

target_ulong helper_lddir(CPULoongArchState *env, target_ulong base,
                          uint32_t level, uint32_t mem_idx)
{
    CPUState *cs = env_cpu(env);
    uint64_t badvaddr;
    hwaddr index, phys;
    uint64_t palen_mask = loongarch_palen_mask(env);
    uint64_t dir_base, dir_width;
    uint64_t val;

    if (unlikely((level == 0) || (level > 4))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attepted LDDIR with level %u\n", level);
        return base;
    }

    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        if (unlikely(level == 4)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Attempted use of level 4 huge page\n");
            return base;
        }

        if (FIELD_EX64(base, TLBENTRY, LEVEL)) {
            return base;
        } else {
            return FIELD_DP64(base, TLBENTRY, LEVEL, level);
        }
    }

    badvaddr = GET_CSR_IF(env->guest, TLBRBADV);
    base = base & palen_mask;
    get_dir_base_width(env, &dir_base, &dir_width, level, env->guest);
    index = (badvaddr >> dir_base) & ((1 << dir_width) - 1);
    phys = base | index << 3;
    val = address_space_ldq_le(
        cs->as,
        (env->guest ? loongarch_get_host_address(env, phys, GETPC()) : phys),
        MEMTXATTRS_UNSPECIFIED, NULL);

    return val & palen_mask;
}

void helper_ldpte(CPULoongArchState *env, target_ulong base, target_ulong odd,
                  uint32_t mem_idx)
{
    CPUState *cs = env_cpu(env);
    hwaddr phys, tmp0, ptindex, ptoffset0, ptoffset1;
    uint64_t pte_raw;
    uint64_t badv;
    uint64_t ptbase =
        FIELD_EX64(GET_CSR_IF(env->guest, PWCL), CSR_PWCL, PTBASE);
    uint64_t ptwidth =
        FIELD_EX64(GET_CSR_IF(env->guest, PWCL), CSR_PWCL, PTWIDTH);
    uint64_t palen_mask = loongarch_palen_mask(env);
    uint64_t dir_base, dir_width;
    uint8_t  ps;


    /*
     * The parameter "base" has only two types,
     * one is the page table base address,
     * whose bit 6 should be 0,
     * and the other is the huge page entry,
     * whose bit 6 should be 1.
     */
    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        /*
         * Gets the huge page level and Gets huge page size.
         * Clears the huge page level information in the entry.
         * Clears huge page bit.
         * Move HGLOBAL bit to GLOBAL bit.
         */
        get_dir_base_width(env, &dir_base, &dir_width,
                           FIELD_EX64(base, TLBENTRY, LEVEL), env->guest);

        base = FIELD_DP64(base, TLBENTRY, LEVEL, 0);
        base = FIELD_DP64(base, TLBENTRY, HUGE, 0);
        if (FIELD_EX64(base, TLBENTRY, HGLOBAL)) {
            base = FIELD_DP64(base, TLBENTRY, HGLOBAL, 0);
            base = FIELD_DP64(base, TLBENTRY, G, 1);
        }

        ps = dir_base + dir_width - 1;
        /*
         * Huge pages are evenly split into parity pages
         * when loaded into the tlb,
         * so the tlb page size needs to be divided by 2.
         */
        tmp0 = loongarch_sanitize_hw_pte(env, base);
        if (odd) {
            tmp0 += MAKE_64BIT_MASK(ps, 1);
        }

        if (!check_ps(env, ps)) {
            qemu_log_mask(LOG_GUEST_ERROR, "Illegal huge pagesize %d\n", ps);
            return;
        }
    } else {
        badv = GET_CSR_IF(env->guest, TLBRBADV);

        base = base & palen_mask;

        ptindex = (badv >> ptbase) & ((1 << ptwidth) - 1);
        ptindex = ptindex & ~0x1;   /* clear bit 0 */
        ptoffset0 = ptindex << 3;
        ptoffset1 = (ptindex + 1) << 3;
        phys = base | (odd ? ptoffset1 : ptoffset0);
        pte_raw = address_space_ldq_le(
            cs->as,
            (env->guest ? loongarch_get_host_address(env, phys, GETPC()) :
                          phys),
            MEMTXATTRS_UNSPECIFIED, NULL);
        tmp0 = loongarch_sanitize_hw_pte(env, pte_raw);
        ps = ptbase;
    }

    if (odd) {
        SET_CSR_IF(env->guest, TLBRELO1, tmp0);
    } else {
        SET_CSR_IF(env->guest, TLBRELO0, tmp0);
    }
    SET_CSR_IF(
        env->guest, TLBREHI,
        FIELD_DP64(GET_CSR_IF(env->guest, TLBREHI), CSR_TLBREHI, PS, ps));
}

static TLBRet loongarch_map_tlb_entry(CPULoongArchState *env,
                                      MMUContext *context,
                                      MMUAccessType access_type, int index,
                                      int mmu_idx, bool guest)
{
    LoongArchTLB *tlb = guest ? &env->gtlb[index] : &env->tlb[index];
    uint8_t tlb_ps, n;

    tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    n = (context->addr >> tlb_ps) & 0x1;/* Odd or even */
    context->pte = n ? tlb->tlb_entry1 : tlb->tlb_entry0;
    context->ps = tlb_ps;
    context->tlb_index = index;
    return loongarch_check_pte(env, context, access_type, mmu_idx, guest);
}

TLBRet loongarch_get_addr_from_tlb(CPULoongArchState *env, MMUContext *context,
                                   MMUAccessType access_type, int mmu_idx,
                                   bool guest)
{
    int index, match;

    match =
        loongarch_tlb_search(env, context->addr, &index, guest, get_tgid(env));
    if (match) {
        return loongarch_map_tlb_entry(env, context, access_type, index,
                                       mmu_idx, guest);
    }

    return TLBRET_NOMATCH;
}
