/*
 * QEMU RISC-V Smmpt (Memory Protection Table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the MPT lookup algorithm per SMMTT specification v0.4.9.
 *
 * MPTE format (v0.4.9):
 *
 * 32-bit non-leaf:  V[0], L=0[1], Reserved[9:2], PPN[31:10]
 * 32-bit non-NAPOT leaf: V[0], L=1[1], N=0[2], Reserved[7:3], XWR[31:8]
 * 32-bit NAPOT leaf: V[0], L=1[1], N=1[2], Reserved[7:3], XWR[10:8], 0[11],
 *                    G[15:12], Reserved[31:16]
 *
 * 64-bit non-leaf:  V[0], L=0[1], Reserved[9:2], PPN[53:10], Reserved[63:54]
 * 64-bit non-NAPOT leaf: V[0], L=1[1], N=0[2], Reserved[7:3], XWR[55:8],
 *                         Reserved[63:56]
 * 64-bit NAPOT leaf: V[0], L=1[1], N=1[2], Reserved[7:3], XWR[10:8], 0[11],
 *                    G[15:12], Reserved[63:16]
 */

#include "qemu/osdep.h"
#include "riscv_smmpt.h"
#include "tcg/pmp.h"
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

static inline bool mpte_is_valid(uint64_t mpte)
{
    return mpte & 0x1;
}

static inline bool mpte_is_leaf(uint64_t mpte)
{
    return mpte & 0x2;
}

static inline bool mpte_get_n(uint64_t mpte)
{
    return (mpte >> 2) & 0x1;
}

/*
 * Get reserved bits from MPTE. Returns non-zero if any reserved bit is set.
 */
static uint64_t mpte_get_rsv(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    bool leaf = mpte_is_leaf(mpte);
    bool napot = mpte_get_n(mpte);

    if (mxl == MXL_RV32) {
        if (!leaf) {
            /* non-leaf32: Reserved = bits[9:2] */
            return extract32(mpte, 2, 8);
        }
        if (!napot) {
            /* non-NAPOT leaf32: Reserved = bits[7:3] (XWR fills [31:8]) */
            return extract32(mpte, 3, 5);
        }
        /* NAPOT leaf32: Reserved = bits[7:3] | bit[11] (mbz) | bits[31:16] */
        return extract32(mpte, 3, 5) | extract32(mpte, 11, 1) |
               extract32(mpte, 16, 16);
    }

    /* RV64 */
    if (!leaf) {
        /* non-leaf64: Reserved = bits[9:2] | bits[63:54] */
        return extract64(mpte, 2, 8) | extract64(mpte, 54, 10);
    }
    if (!napot) {
        /* non-NAPOT leaf64: Reserved = bits[7:3] | bits[63:56] */
        return extract64(mpte, 3, 5) | extract64(mpte, 56, 8);
    }
    /* NAPOT leaf64: Reserved = bits[7:3] | bit[11] (mbz) | bits[63:16] */
    return extract64(mpte, 3, 5) | extract64(mpte, 11, 1) |
           extract64(mpte, 16, 48);
}

/*
 * Get PPN from a non-leaf MPTE.
 * RV32 non-leaf: PPN = bits[31:10] (22 bits)
 * RV64 non-leaf: PPN = bits[53:10] (44 bits)
 */
static uint64_t mpte_get_ppn(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);

    if (mxl == MXL_RV32) {
        return extract32(mpte, 10, 22);
    }
    return extract64(mpte, 10, 44);
}

/*
 * Get XWR permission for a specific page index from a non-NAPOT leaf.
 * The XWR base bit is 8 for both RV32 and RV64; only the number of
 * entries differs (pi in 0..7 for RV32, 0..15 for RV64).
 */
static uint32_t mpte_get_xwr(CPURISCVState *env, uint64_t mpte, int pi)
{
    return extract64(mpte, 8 + pi * 3, 3);
}

/*
 * Get single XWR from a NAPOT leaf.
 * The NAPOT XWR base bit is 8 for both RV32 and RV64 (bits[10:8]).
 */
static uint32_t mpte_get_napot_xwr(CPURISCVState *env, uint64_t mpte)
{
    return extract64(mpte, 8, 3);
}

/*
 * Get G field from a NAPOT leaf.
 * The G base bit is 12 for both RV32 and RV64 (bits[15:12]).
 */
static uint32_t mpte_get_g(CPURISCVState *env, uint64_t mpte)
{
    return extract64(mpte, 12, 4);
}

/*
 * Validate the G encoding for the given MPT mode.
 * Smmpt34: only G=6 is valid
 * Smmpt43/52/64: only G=4 is valid
 */
static bool mpte_validate_g(uint32_t g, mpt_mode_t mode)
{
    switch (mode) {
    case SMMPT34:
        return g == 6;
    case SMMPT43:
    case SMMPT52:
    case SMMPT64:
        return g == 4;
    default:
        return false;
    }
}

/*
 * Get page number index pn[i] from the supervisor physical address.
 *
 * Smmpt34 (34-bit SPA):
 *   SPA layout: range_offset[14:0], pn[0][24:15] (10 bits),
 *   pn[1][33:25] (9 bits)
 *
 * Smmpt43/52/64 (RV64 MPT):
 *   SPA layout: range_offset[15:0], pn[0][24:16] (9 bits), ...
 *   For Smmpt64, pn[4] (top level) is 12 bits.
 */
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

/*
 * Get the page index within a leaf MPTE.
 *
 * Smmpt34: pi = SPA[14:12] (3 bits) for level 0, SPA[24:22] for level 1
 * Smmpt43/52/64: pi = SPA[offset-4 +: 4] (4 bits)
 */
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

/*
 * Check XWR permission bits against the access type.
 * Returns true if access is allowed.
 *
 * The 3-bit XWR field uses the same bit order as RISC-V PTE permissions
 * (bit0 = R, bit1 = W, bit2 = X), matching QEMU's PAGE_READ / PAGE_WRITE /
 * PAGE_EXEC. As with PTEs, writable-but-not-readable encodings are reserved.
 *
 * XWR encoding (bit2=X, bit1=W, bit0=R):
 *   000 = No access
 *   001 = Read only
 *   010 = Reserved (fault)
 *   011 = Read + Write
 *   100 = Execute only
 *   101 = Read + Execute
 *   110 = Reserved (fault)
 *   111 = Read + Write + Execute
 */
static bool mpt_check_xwr(uint32_t xwr, int *prot, MMUAccessType access_type)
{
    switch (xwr) {
    case 0: /* No access */
        return false;
    case PAGE_EXEC: /* 100: Execute only */
        *prot = PAGE_EXEC;
        return access_type == MMU_INST_FETCH;
    case PAGE_READ | PAGE_EXEC: /* 101: Read + Execute */
        *prot = PAGE_READ | PAGE_EXEC;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_INST_FETCH);
    case PAGE_READ: /* 001: Read only */
        *prot = PAGE_READ;
        return access_type == MMU_DATA_LOAD;
    case PAGE_READ | PAGE_WRITE: /* 011: Read + Write */
        *prot = PAGE_READ | PAGE_WRITE;
        return (access_type == MMU_DATA_LOAD ||
                access_type == MMU_DATA_STORE);
    case PAGE_READ | PAGE_WRITE | PAGE_EXEC: /* 111: R+W+X */
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return true;
    default: /* 010, 110: Reserved - fault */
        return false;
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
    uint32_t mptesize, levels, xwr, g;
    int pn, pi, pmp_prot, pmp_ret;
    uint64_t mpte;

    switch (mode) {
    case SMMPT34:
        load_entry = &load_entry_32; levels = 2; mptesize = 4; break;
    case SMMPT43:
        load_entry = &load_entry_64; levels = 3; mptesize = 8; break;
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

    for (int i = levels - 1; i >= 0; i--) {
        /* Step 1: Get pn[i] as the MPT index */
        pn = mpt_get_pn(addr, i, mode);

        /* Step 2: Load MPTE from memory */
        mpte_addr = base + pn * mptesize;
        pmp_ret = get_physical_address_pmp(env, &pmp_prot, mpte_addr,
                                           mptesize, MMU_DATA_LOAD, PRV_M);
        if (pmp_ret != TRANSLATE_SUCCESS) {
            return false;
        }
        mpte = load_entry(cs->as, mpte_addr, attrs, &res);
        if (res != MEMTX_OK) {
            return false;
        }

        /* Step 3: Check valid bit and reserved bits */
        if (!mpte_is_valid(mpte) || mpte_get_rsv(env, mpte)) {
            return false;
        }

        /* Step 3 (cont): non-leaf with N=1 is a fault */
        if (!mpte_is_leaf(mpte) && mpte_get_n(mpte)) {
            return false;
        }

        /* Step 4: Process non-leaf node */
        if (!mpte_is_leaf(mpte)) {
            if (i == 0) {
                return false;
            }
            base = mpte_get_ppn(env, mpte) << PGSHIFT;
            continue;
        }

        /* Step 5 & 6: Process leaf node */
        if (!mpte_get_n(mpte)) {
            /* Step 5: Non-NAPOT leaf - get XWR[pi] */
            pi = mpt_get_pi(addr, i, mode);
            xwr = mpte_get_xwr(env, mpte, pi);
        } else {
            /* Step 6: NAPOT leaf - validate G, get single XWR */
            g = mpte_get_g(env, mpte);
            if (!mpte_validate_g(g, mode)) {
                return false;
            }
            xwr = mpte_get_napot_xwr(env, mpte);
        }

        /* Step 7: Check permission */
        return mpt_check_xwr(xwr, prot, access_type);
    }
    return false;
}

bool smmpt_check_access(CPURISCVState *env, hwaddr addr,
                        int *prot, MMUAccessType access_type)
{
    mpt_mode_t mode = env->mptmode;

    return smmpt_lookup(env, addr, mode, prot, access_type);
}
