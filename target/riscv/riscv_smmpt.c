/*
 * QEMU RISC-V Smmpt (Memory Protection Table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "riscv_smmpt.h"
#include "pmp.h"
#include "system/memory.h"

typedef uint64_t load_entry_fn(AddressSpace *, hwaddr,
                               MemTxAttrs, MemTxResult *);

static uint64_t load_entry_32(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldl(as, addr, attrs, result);
}

static uint64_t load_entry_64(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldq(as, addr, attrs, result);
}

typedef union {
    uint64_t raw;
    struct {
        uint32_t v:1;
        uint32_t l:1;
        uint32_t rsv1:5;
        uint32_t perms:24;
        uint32_t n:1;
    } leaf32;
    struct {
        uint32_t v:1;
        uint32_t l:1;
        uint32_t rsv1:8;
        uint32_t ppn:22;
    } nonleaf32;
    struct {
        uint64_t v:1;
        uint64_t l:1;
        uint64_t rsv1:8;
        uint64_t perms:48;
        uint64_t rsv2:5;
        uint64_t n:1;
    } leaf64;
    struct {
        uint64_t v:1;
        uint64_t l:1;
        uint64_t rsv1:8;
        uint64_t ppn:52;
        uint64_t rsv2:1;
        uint64_t n:1;
    } nonleaf64;
} mpte_union_t;

static inline bool mpte_is_leaf(uint64_t mpte)
{
   return mpte & 0x2;
}

static inline bool mpte_is_valid(uint64_t mpte)
{
    return mpte & 0x1;
}

static uint64_t mpte_get_rsv(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    bool leaf = mpte_is_leaf(mpte);
    mpte_union_t *u = (mpte_union_t *)&mpte;

    if (mxl == MXL_RV32) {
        return leaf ? u->leaf32.rsv1 : u->nonleaf32.rsv1;
    }
    return leaf ? (u->leaf64.rsv1 << 5) | u->leaf64.rsv2
                : (u->nonleaf64.rsv1 << 1) | u->nonleaf64.rsv2;
}

static uint64_t mpte_get_perms(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    mpte_union_t *u = (mpte_union_t *)&mpte;

    return (mxl == MXL_RV32) ? u->leaf32.perms : u->leaf64.perms;
}

static bool mpte_check_nlnapot(CPURISCVState *env, uint64_t mpte, bool *nlnapot)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    mpte_union_t *u = (mpte_union_t *)&mpte;
    if (mxl == MXL_RV32) {
        *nlnapot = false;
        return true;
    }
    *nlnapot = u->nonleaf64.n;
    return u->nonleaf64.n ? (u->nonleaf64.ppn & 0x1ff) == 0x100 : true;
}

static uint64_t mpte_get_ppn(CPURISCVState *env, uint64_t mpte, int pn,
                             bool nlnapot)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    mpte_union_t *u = (mpte_union_t *)&mpte;

    if (nlnapot) {
        return deposit64(u->nonleaf64.ppn, 0, 9, pn & 0x1ff);
    }
    return (mxl == MXL_RV32) ? u->nonleaf32.ppn : u->nonleaf64.ppn;
}

/* Caller should assert i before call this interface */
static int mpt_get_pn(hwaddr addr, int i, mpt_mode_t mode)
{
    if (mode == SMMPT34) {
        return i == 0
            ? extract64(addr, 15, 10)
            : extract64(addr, 25, 9);
    } else {
        int offset = 16 + i * 9;
        if ((mode == SMMPT64) && (i == 4)) {
            return extract64(addr, offset, 12);
        } else {
            return extract64(addr, offset, 9);
        }
    }
}

/* Caller should assert i before call this interface */
static int mpt_get_pi(hwaddr addr, int i, mpt_mode_t mode)
{
    if (mode == SMMPT34) {
        return i == 0
            ? extract64(addr, 12, 3)
            : extract64(addr, 22, 3);
    } else {
        int offset = 16 + i * 9;
        return extract64(addr, offset - 4, 4);
    }
}

static bool smmpt_lookup(CPURISCVState *env, hwaddr addr, mpt_mode_t mode,
                         mpt_access_t *allowed_access,
                         MMUAccessType access_type)
{
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    CPUState *cs = env_cpu(env);
    hwaddr mpte_addr, base = (hwaddr)env->mptppn << PGSHIFT;
    load_entry_fn *load_entry;
    uint32_t mptesize, levels, xwr;
    int pn, pi, pmp_prot, pmp_ret;
    uint64_t mpte, perms;

    switch (mode) {
    case SMMPT34:
        load_entry = &load_entry_32; levels = 2; mptesize = 4; break;
    case SMMPT43:
        load_entry = &load_entry_64; levels = 3; mptesize = 8; break;
        break;
    case SMMPT52:
        load_entry = &load_entry_64; levels = 4; mptesize = 8; break;
    case SMMPT64:
        load_entry = &load_entry_64; levels = 5; mptesize = 8; break;
    case SMMPTBARE:
        *allowed_access = ACCESS_ALLOW_RWX;
        return true;
    default:
        g_assert_not_reached();
        break;
    }
    for (int i = levels - 1; i >= 0 ; i--) {
        /* 1. Get pn[i] as the mpt index */
        pn = mpt_get_pn(addr, i, mode);
        /* 2. Get mpte address and get mpte */
        mpte_addr = base + pn * mptesize;
        pmp_ret = get_physical_address_pmp(env, &pmp_prot, mpte_addr,
                                           mptesize, MMU_DATA_LOAD, PRV_M);
        if (pmp_ret != TRANSLATE_SUCCESS) {
            return false;
        }
        mpte = load_entry(cs->as, mpte_addr, attrs, &res);
        /* 3. Check valid bit and reserve bits of mpte */
        if (!mpte_is_valid(mpte) || mpte_get_rsv(env, mpte)) {
            return false;
        }

        /* 4. Process non-leaf node */
        if (!mpte_is_leaf(mpte)) {
            bool nlnapot = false;
            if (i == 0) {
                return false;
            }
            if (!mpte_check_nlnapot(env, mpte, &nlnapot)) {
                return false;
            }
            base = mpte_get_ppn(env, mpte, pn, nlnapot) << PGSHIFT;
            continue;
        }

        /* 5. Process leaf node */
        pi = mpt_get_pi(addr, i, mode);
        perms = mpte_get_perms(env, mpte);
        xwr = (perms >> (pi * 3)) & 0x7;
        switch (xwr) {
        case ACCESS_ALLOW_R:
            *allowed_access = ACCESS_ALLOW_R;
            return access_type == MMU_DATA_LOAD;
        case ACCESS_ALLOW_X:
            *allowed_access = ACCESS_ALLOW_X;
            return access_type == MMU_INST_FETCH;
        case ACCESS_ALLOW_RX:
            *allowed_access = ACCESS_ALLOW_R;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_INST_FETCH);
        case ACCESS_ALLOW_RW:
            *allowed_access = ACCESS_ALLOW_RW;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_DATA_STORE);
        case ACCESS_ALLOW_RWX:
            *allowed_access = ACCESS_ALLOW_RWX;
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool smmpt_check_access(CPURISCVState *env, hwaddr addr,
                        mpt_access_t *allowed_access, MMUAccessType access_type)
{
    bool mpt_has_access;
    mpt_mode_t mode = env->mptmode;

    mpt_has_access = smmpt_lookup(env, addr, mode,
                                  allowed_access, access_type);
    return mpt_has_access;
}

/*
 * Convert MPT access to TLB page privilege.
 */
int smmpt_access_to_page_prot(mpt_access_t mpt_access)
{
    int prot;
    switch (mpt_access) {
    case ACCESS_ALLOW_R:
        prot = PAGE_READ;
        break;
    case ACCESS_ALLOW_X:
        prot = PAGE_EXEC;
        break;
    case ACCESS_ALLOW_RX:
        prot = PAGE_READ | PAGE_EXEC;
        break;
    case ACCESS_ALLOW_RW:
        prot = PAGE_READ | PAGE_WRITE;
        break;
    case ACCESS_ALLOW_RWX:
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        break;
    default:
        prot = 0;
        break;
    }

    return prot;
}
