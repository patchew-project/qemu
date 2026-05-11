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
#include "exec/page-protection.h"
#include "system/memory.h"

typedef uint64_t load_entry_fn(AddressSpace *, hwaddr,
                               MemTxAttrs, MemTxResult *);

static uint64_t load_entry_32(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldl_le(as, addr, attrs, result);
}

static uint64_t load_entry_64(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldq_le(as, addr, attrs, result);
}

/*
 * MPT entry bit field positions:
 *
 * 32-bit leaf:     v[0], l[1], rsv1[6:2], perms[30:7], n[31]
 * 32-bit nonleaf:  v[0], l[1], rsv1[9:2], ppn[31:10]
 * 64-bit leaf:     v[0], l[1], rsv1[9:2], perms[57:10], rsv2[62:58], n[63]
 * 64-bit nonleaf:  v[0], l[1], rsv1[9:2], ppn[61:10], rsv2[62], n[63]
 */

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

    if (mxl == MXL_RV32) {
        /* leaf32.rsv1 = bits[6:2], nonleaf32.rsv1 = bits[9:2] */
        return leaf ? extract32(mpte, 2, 5) : extract32(mpte, 2, 8);
    }
    if (leaf) {
        /* leaf64.rsv1 = bits[9:2], leaf64.rsv2 = bits[62:58] */
        return (extract64(mpte, 2, 8) << 5) | extract64(mpte, 58, 5);
    }
    /* nonleaf64.rsv1 = bits[9:2], nonleaf64.rsv2 = bit[62] */
    return (extract64(mpte, 2, 8) << 1) | extract64(mpte, 62, 1);
}

static uint64_t mpte_get_perms(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);

    /* leaf32.perms = bits[30:7], leaf64.perms = bits[57:10] */
    return (mxl == MXL_RV32) ? extract32(mpte, 7, 24) : extract64(mpte, 10, 48);
}

static bool mpte_check_nlnapot(CPURISCVState *env, uint64_t mpte, bool *nlnapot)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    uint64_t n_bit, ppn;

    if (mxl == MXL_RV32) {
        *nlnapot = false;
        return true;
    }
    /* nonleaf64.n = bit[63], nonleaf64.ppn = bits[61:10] */
    n_bit = extract64(mpte, 63, 1);
    ppn = extract64(mpte, 10, 52);
    *nlnapot = n_bit;
    return n_bit ? (ppn & 0x1ff) == 0x100 : true;
}

static uint64_t mpte_get_ppn(CPURISCVState *env, uint64_t mpte, int pn,
                             bool nlnapot)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    /* nonleaf64.ppn = bits[61:10], nonleaf32.ppn = bits[31:10] */
    uint64_t ppn = (mxl == MXL_RV32) ? extract32(mpte, 10, 22)
                                     : extract64(mpte, 10, 52);

    if (nlnapot) {
        return deposit64(ppn, 0, 9, pn & 0x1ff);
    }
    return ppn;
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
                         int *prot, MMUAccessType access_type)
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
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
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
        case PAGE_READ:
            *prot = PAGE_READ;
            return access_type == MMU_DATA_LOAD;
        case PAGE_EXEC:
            *prot = PAGE_EXEC;
            return access_type == MMU_INST_FETCH;
        case PAGE_READ | PAGE_EXEC:
            *prot = PAGE_READ | PAGE_EXEC;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_INST_FETCH);
        case PAGE_READ | PAGE_WRITE:
            *prot = PAGE_READ | PAGE_WRITE;
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_DATA_STORE);
        case PAGE_READ | PAGE_WRITE | PAGE_EXEC:
            *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool smmpt_check_access(CPURISCVState *env, hwaddr addr,
                        int *prot, MMUAccessType access_type)
{
    mpt_mode_t mode = env->mptmode;

    return smmpt_lookup(env, addr, mode, prot, access_type);
}
