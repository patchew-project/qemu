/*
 * ARM SME Operations
 *
 * Copyright (c) 2022 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "qemu/int128.h"
#include "vec_internal.h"
#include "sve_ldst_internal.h"

/* ResetSVEState */
void arm_reset_sve_state(CPUARMState *env)
{
    memset(env->vfp.zregs, 0, sizeof(env->vfp.zregs));
    memset(env->vfp.pregs, 0, sizeof(env->vfp.pregs));
    vfp_set_fpcr(env, 0x0800009f);
}

void helper_set_pstate_sm(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, SM)) {
        return;
    }
    env->svcr ^= R_SVCR_SM_MASK;
    arm_reset_sve_state(env);
}

void helper_set_pstate_za(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, ZA)) {
        return;
    }
    env->svcr ^= R_SVCR_ZA_MASK;

    /*
     * ResetSMEState.
     *
     * SetPSTATE_ZA zeros on enable and disable.  It would appear that we
     * can zero this only on enable: while disabled, the storage is
     * inaccessible and the value does not matter.  We're not saving the
     * storage in vmstate when disabled either.
     */
    if (i) {
        memset(env->zarray, 0, sizeof(env->zarray));
    }
}

void helper_sme_zero(CPUARMState *env, uint32_t imm, uint32_t svl)
{
    uint32_t i;

    /*
     * Special case clearing the entire ZA space.
     * This falls into the CONSTRAINED UNPREDICTABLE zeroing of any
     * parts of the ZA storage outside of SVL.
     */
    if (imm == 0xff) {
        memset(env->zarray, 0, sizeof(env->zarray));
        return;
    }

    /*
     * Recall that ZAnH.D[m] is spread across ZA[n+8*m].
     * Unless SVL == ARM_MAX_VQ, each row is discontiguous.
     */
    for (i = 0; i < svl; i++) {
        if (imm & (1 << (i % 8))) {
            memset(&env->zarray[i], 0, svl);
        }
    }
}

#define DO_MOVA_A(NAME, TYPE, H)                                        \
void HELPER(NAME)(void *za, void *vn, void *vg, uint32_t desc)          \
{                                                                       \
    int i, oprsz = simd_oprsz(desc);                                    \
    for (i = 0; i < oprsz; ) {                                          \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                *(TYPE *)za = *(TYPE *)(vn + H(i));                     \
            }                                                           \
            za += sizeof(ARMVectorReg) * sizeof(TYPE);                  \
            i += sizeof(TYPE);                                          \
            pg >>= sizeof(TYPE);                                        \
        } while (i & 15);                                               \
    }                                                                   \
}

#define DO_MOVA_Z(NAME, TYPE, H)                                        \
void HELPER(NAME)(void *vd, void *za, void *vg, uint32_t desc)          \
{                                                                       \
    int i, oprsz = simd_oprsz(desc);                                    \
    for (i = 0; i < oprsz; ) {                                          \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                *(TYPE *)(vd + H(i)) = *(TYPE *)za;                     \
            }                                                           \
            za += sizeof(ARMVectorReg) * sizeof(TYPE);                  \
            i += sizeof(TYPE);                                          \
            pg >>= sizeof(TYPE);                                        \
        } while (i & 15);                                               \
    }                                                                   \
}

DO_MOVA_A(sme_mova_avz_b, uint8_t, H1)
DO_MOVA_A(sme_mova_avz_h, uint16_t, H2)
DO_MOVA_A(sme_mova_avz_s, uint32_t, H4)

DO_MOVA_Z(sme_mova_zav_b, uint8_t, H1)
DO_MOVA_Z(sme_mova_zav_h, uint16_t, H2)
DO_MOVA_Z(sme_mova_zav_s, uint32_t, H4)

void HELPER(sme_mova_avz_d)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pg = vg;
    uint64_t *n = vn;
    uint64_t *a = za;

    /*
     * Note that the rows of the ZAV.D tile are 8 absolute rows apart,
     * so while the address arithmetic below looks funny, it is right.
     */
    for (i = 0; i < oprsz; i++) {
        if (pg[H1_2(i)] & 1) {
            a[i * sizeof(ARMVectorReg)] = n[i];
        }
    }
}

void HELPER(sme_mova_zav_d)(void *vd, void *za, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pg = vg;
    uint64_t *d = vd;
    uint64_t *a = za;

    for (i = 0; i < oprsz; i++) {
        if (pg[H1_2(i)] & 1) {
            d[i] = a[i * sizeof(ARMVectorReg)];
        }
    }
}

void HELPER(sme_mova_avz_q)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 16;
    uint16_t *pg = vg;
    Int128 *n = vn;
    Int128 *a = za;

    /*
     * Note that the rows of the ZAV.Q tile are 16 absolute rows apart,
     * so while the address arithmetic below looks funny, it is right.
     */
    for (i = 0; i < oprsz; i++) {
        if (pg[H2(i)] & 1) {
            a[i * sizeof(ARMVectorReg)] = n[i];
        }
    }
}

void HELPER(sme_mova_zav_q)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 16;
    uint16_t *pg = vg;
    Int128 *n = vn;
    Int128 *a = za;

    for (i = 0; i < oprsz; i++, za += sizeof(ARMVectorReg)) {
        if (pg[H2(i)] & 1) {
            n[i] = a[i * sizeof(ARMVectorReg)];
        }
    }
}

/*
 * Clear elements in a tile slice comprising len bytes.
 */

typedef void ClearFn(void *ptr, size_t off, size_t len);

static void clear_horizontal(void *ptr, size_t off, size_t len)
{
    memset(ptr + off, 0, len);
}

static void clear_vertical_b(void *vptr, size_t off, size_t len)
{
    uint8_t *ptr = vptr;
    size_t i;

    for (i = 0; i < len; ++i) {
        ptr[(i + off) * sizeof(ARMVectorReg)] = 0;
    }
}

static void clear_vertical_h(void *vptr, size_t off, size_t len)
{
    uint16_t *ptr = vptr;
    size_t i;

    for (i = 0; i < len / 2; ++i) {
        ptr[(i + off) * sizeof(ARMVectorReg)] = 0;
    }
}

static void clear_vertical_s(void *vptr, size_t off, size_t len)
{
    uint32_t *ptr = vptr;
    size_t i;

    for (i = 0; i < len / 4; ++i) {
        ptr[(i + off) * sizeof(ARMVectorReg)] = 0;
    }
}

static void clear_vertical_d(void *vptr, size_t off, size_t len)
{
    uint64_t *ptr = vptr;
    size_t i;

    for (i = 0; i < len / 8; ++i) {
        ptr[(i + off) * sizeof(ARMVectorReg)] = 0;
    }
}

static void clear_vertical_q(void *vptr, size_t off, size_t len)
{
    Int128 *ptr = vptr, zero = int128_zero();
    size_t i;

    for (i = 0; i < len / 16; ++i) {
        ptr[(i + off) * sizeof(ARMVectorReg)] = zero;
    }
}

/*
 * Copy elements from an array into a tile slice comprising len bytes.
 */

typedef void CopyFn(void *dst, const void *src, size_t len);

static void copy_horizontal(void *dst, const void *src, size_t len)
{
    memcpy(dst, src, len);
}

static void copy_vertical_b(void *vdst, const void *vsrc, size_t len)
{
    const uint8_t *src = vsrc;
    uint8_t *dst = vdst;
    size_t i;

    for (i = 0; i < len; ++i) {
        dst[i * sizeof(ARMVectorReg)] = src[i];
    }
}

static void copy_vertical_h(void *vdst, const void *vsrc, size_t len)
{
    const uint16_t *src = vsrc;
    uint16_t *dst = vdst;
    size_t i;

    for (i = 0; i < len / 2; ++i) {
        dst[i * sizeof(ARMVectorReg)] = src[i];
    }
}

static void copy_vertical_s(void *vdst, const void *vsrc, size_t len)
{
    const uint32_t *src = vsrc;
    uint32_t *dst = vdst;
    size_t i;

    for (i = 0; i < len / 4; ++i) {
        dst[i * sizeof(ARMVectorReg)] = src[i];
    }
}

static void copy_vertical_d(void *vdst, const void *vsrc, size_t len)
{
    const uint64_t *src = vsrc;
    uint64_t *dst = vdst;
    size_t i;

    for (i = 0; i < len / 8; ++i) {
        dst[i * sizeof(ARMVectorReg)] = src[i];
    }
}

static void copy_vertical_q(void *vdst, const void *vsrc, size_t len)
{
    const Int128 *src = vsrc;
    Int128 *dst = vdst;
    size_t i;

    for (i = 0; i < len / 16; ++i) {
        dst[i * sizeof(ARMVectorReg)] = src[i];
    }
}

/*
 * Host and TLB primitives for vertical tile slice addressing.
 */

#define DO_LD(NAME, TYPE, HOST, TLB)                                        \
static inline void sme_##NAME##_v_host(void *za, intptr_t off, void *host)  \
{                                                                           \
    TYPE val = HOST(host);                                                  \
    *(TYPE *)(za + off * sizeof(ARMVectorReg)) = val;                       \
}                                                                           \
static inline void sme_##NAME##_v_tlb(CPUARMState *env, void *za,           \
                        intptr_t off, target_ulong addr, uintptr_t ra)      \
{                                                                           \
    TYPE val = TLB(env, useronly_clean_ptr(addr), ra);                      \
    *(TYPE *)(za + off * sizeof(ARMVectorReg)) = val;                       \
}

#define DO_ST(NAME, TYPE, HOST, TLB)                                        \
static inline void sme_##NAME##_v_host(void *za, intptr_t off, void *host)  \
{                                                                           \
    TYPE val = *(TYPE *)(za + off * sizeof(ARMVectorReg));                  \
    HOST(host, val);                                                        \
}                                                                           \
static inline void sme_##NAME##_v_tlb(CPUARMState *env, void *za,           \
                        intptr_t off, target_ulong addr, uintptr_t ra)      \
{                                                                           \
    TYPE val = *(TYPE *)(za + off * sizeof(ARMVectorReg));                  \
    TLB(env, useronly_clean_ptr(addr), val, ra);                            \
}

/*
 * FIXME: The ARMVectorReg elements are stored in host-endian 64-bit units.
 * We do not have a defined ordering of the 64-bit units for host-endian
 * 128-bit quantities.  For now, just leave the host words in little-endian
 * order and hope for the best.
 */
#define DO_LDQ(HNAME, VNAME, BE, HOST, TLB)                                 \
static inline void HNAME##_host(void *za, intptr_t off, void *host)         \
{                                                                           \
    uint64_t val0 = HOST(host), val1 = HOST(host + 8);                      \
    uint64_t *ptr = za + off;                                               \
    ptr[0] = BE ? val1 : val0, ptr[1] = BE ? val0 : val1;                   \
}                                                                           \
static inline void VNAME##_v_host(void *za, intptr_t off, void *host)       \
{                                                                           \
    HNAME##_host(za, off * sizeof(ARMVectorReg), host);                     \
}                                                                           \
static inline void HNAME##_tlb(CPUARMState *env, void *za, intptr_t off,    \
                               target_ulong addr, uintptr_t ra)             \
{                                                                           \
    uint64_t val0 = TLB(env, useronly_clean_ptr(addr), ra);                 \
    uint64_t val1 = TLB(env, useronly_clean_ptr(addr + 8), ra);             \
    uint64_t *ptr = za + off;                                               \
    ptr[0] = BE ? val1 : val0, ptr[1] = BE ? val0 : val1;                   \
}                                                                           \
static inline void VNAME##_v_tlb(CPUARMState *env, void *za, intptr_t off,  \
                               target_ulong addr, uintptr_t ra)             \
{                                                                           \
    HNAME##_tlb(env, za, off * sizeof(ARMVectorReg), addr, ra);             \
}

#define DO_STQ(HNAME, VNAME, BE, HOST, TLB)                                 \
static inline void HNAME##_host(void *za, intptr_t off, void *host)         \
{                                                                           \
    uint64_t *ptr = za + off;                                               \
    HOST(host, ptr[BE]);                                                    \
    HOST(host + 1, ptr[!BE]);                                               \
}                                                                           \
static inline void VNAME##_v_host(void *za, intptr_t off, void *host)       \
{                                                                           \
    HNAME##_host(za, off * sizeof(ARMVectorReg), host);                     \
}                                                                           \
static inline void HNAME##_tlb(CPUARMState *env, void *za, intptr_t off,    \
                               target_ulong addr, uintptr_t ra)             \
{                                                                           \
    uint64_t *ptr = za + off;                                               \
    TLB(env, useronly_clean_ptr(addr), ptr[BE], ra);                        \
    TLB(env, useronly_clean_ptr(addr + 8), ptr[!BE], ra);                   \
}                                                                           \
static inline void VNAME##_v_tlb(CPUARMState *env, void *za, intptr_t off,  \
                               target_ulong addr, uintptr_t ra)             \
{                                                                           \
    HNAME##_tlb(env, za, off * sizeof(ARMVectorReg), addr, ra);             \
}

DO_LD(ld1b, uint8_t, ldub_p, cpu_ldub_data_ra)
DO_LD(ld1h_be, uint16_t, lduw_be_p, cpu_lduw_be_data_ra)
DO_LD(ld1h_le, uint16_t, lduw_le_p, cpu_lduw_le_data_ra)
DO_LD(ld1s_be, uint32_t, ldl_be_p, cpu_ldl_be_data_ra)
DO_LD(ld1s_le, uint32_t, ldl_le_p, cpu_ldl_le_data_ra)
DO_LD(ld1d_be, uint64_t, ldq_be_p, cpu_ldq_be_data_ra)
DO_LD(ld1d_le, uint64_t, ldq_le_p, cpu_ldq_le_data_ra)

DO_LDQ(sve_ld1qq_be, sme_ld1q_be, 1, ldq_be_p, cpu_ldq_be_data_ra)
DO_LDQ(sve_ld1qq_le, sme_ld1q_le, 0, ldq_le_p, cpu_ldq_le_data_ra)

DO_ST(st1b, uint8_t, stb_p, cpu_stb_data_ra)
DO_ST(st1h_be, uint16_t, stw_be_p, cpu_stw_be_data_ra)
DO_ST(st1h_le, uint16_t, stw_le_p, cpu_stw_le_data_ra)
DO_ST(st1s_be, uint32_t, stl_be_p, cpu_stl_be_data_ra)
DO_ST(st1s_le, uint32_t, stl_le_p, cpu_stl_le_data_ra)
DO_ST(st1d_be, uint64_t, stq_be_p, cpu_stq_be_data_ra)
DO_ST(st1d_le, uint64_t, stq_le_p, cpu_stq_le_data_ra)

DO_STQ(sve_st1qq_be, sme_st1q_be, 1, stq_be_p, cpu_stq_be_data_ra)
DO_STQ(sve_st1qq_le, sme_st1q_le, 0, stq_le_p, cpu_stq_le_data_ra)

#undef DO_LD
#undef DO_ST
#undef DO_LDQ
#undef DO_STQ

/*
 * Common helper for all contiguous predicated loads.
 */

static inline QEMU_ALWAYS_INLINE
void sme_ld1(CPUARMState *env, void *za, uint64_t *vg,
             const target_ulong addr, uint32_t desc, const uintptr_t ra,
             const int esz, uint32_t mtedesc, bool vertical,
             sve_ldst1_host_fn *host_fn,
             sve_ldst1_tlb_fn *tlb_fn,
             ClearFn *clr_fn,
             CopyFn *cpy_fn)
{
    const intptr_t reg_max = simd_oprsz(desc);
    const intptr_t esize = 1 << esz;
    intptr_t reg_off, reg_last;
    SVEContLdSt info;
    void *host;
    int flags;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, esize)) {
        /* The entire predicate was false; no load occurs.  */
        clr_fn(za, 0, reg_max);
        return;
    }

    /* Probe the page(s).  Exit with exception for any invalid page. */
    sve_cont_ldst_pages(&info, FAULT_ALL, env, addr, MMU_DATA_LOAD, ra);

    /* Handle watchpoints for all active elements. */
    sve_cont_ldst_watchpoints(&info, env, vg, addr, esize, esize,
                              BP_MEM_READ, ra);

    /*
     * Handle mte checks for all active elements.
     * Since TBI must be set for MTE, !mtedesc => !mte_active.
     */
    if (mtedesc) {
        sve_cont_ldst_mte_check(&info, env, vg, addr, esize, esize,
                                mtedesc, ra);
    }

    flags = info.page[0].flags | info.page[1].flags;
    if (unlikely(flags != 0)) {
#ifdef CONFIG_USER_ONLY
        g_assert_not_reached();
#else
        /*
         * At least one page includes MMIO.
         * Any bus operation can fail with cpu_transaction_failed,
         * which for ARM will raise SyncExternal.  Perform the load
         * into scratch memory to preserve register state until the end.
         */
        ARMVectorReg scratch = { };

        reg_off = info.reg_off_first[0];
        reg_last = info.reg_off_last[1];
        if (reg_last < 0) {
            reg_last = info.reg_off_split;
            if (reg_last < 0) {
                reg_last = info.reg_off_last[0];
            }
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    tlb_fn(env, &scratch, reg_off, addr + reg_off, ra);
                }
                reg_off += esize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        cpy_fn(za, &scratch, reg_max);
        return;
#endif
    }

    /* The entire operation is in RAM, on valid pages. */

    reg_off = info.reg_off_first[0];
    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    if (!vertical) {
        memset(za, 0, reg_max);
    } else if (reg_off) {
        clr_fn(za, 0, reg_off);
    }

    while (reg_off <= reg_last) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                host_fn(za, reg_off, host + reg_off);
            } else if (vertical) {
                clr_fn(za, reg_off, esize);
            }
            reg_off += esize;
        } while (reg_off <= reg_last && (reg_off & 63));
    }

    /*
     * Use the slow path to manage the cross-page misalignment.
     * But we know this is RAM and cannot trap.
     */
    reg_off = info.reg_off_split;
    if (unlikely(reg_off >= 0)) {
        tlb_fn(env, za, reg_off, addr + reg_off, ra);
    }

    reg_off = info.reg_off_first[1];
    if (unlikely(reg_off >= 0)) {
        reg_last = info.reg_off_last[1];
        host = info.page[1].host;

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    host_fn(za, reg_off, host + reg_off);
                } else if (vertical) {
                    clr_fn(za, reg_off, esize);
                }
                reg_off += esize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
    }
}

static inline QEMU_ALWAYS_INLINE
void sme_ld1_mte(CPUARMState *env, void *za, uint64_t *vg,
                 target_ulong addr, uint32_t desc, uintptr_t ra,
                 const int esz, bool vertical,
                 sve_ldst1_host_fn *host_fn,
                 sve_ldst1_tlb_fn *tlb_fn,
                 ClearFn *clr_fn,
                 CopyFn *cpy_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    int bit55 = extract64(addr, 55, 1);

    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(desc, bit55) ||
        tcma_check(desc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sme_ld1(env, za, vg, addr, desc, ra, esz, mtedesc, vertical,
            host_fn, tlb_fn, clr_fn, cpy_fn);
}

#define DO_LD(L, END, ESZ)                                                 \
void HELPER(sme_ld1##L##END##_h)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint32_t desc)         \
{                                                                          \
    sme_ld1(env, za, vg, addr, desc, GETPC(), ESZ, 0, false,               \
            sve_ld1##L##L##END##_host, sve_ld1##L##L##END##_tlb,           \
            clear_horizontal, copy_horizontal);                            \
}                                                                          \
void HELPER(sme_ld1##L##END##_v)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint32_t desc)         \
{                                                                          \
    sme_ld1(env, za, vg, addr, desc, GETPC(), ESZ, 0, true,                \
            sme_ld1##L##END##_v_host, sme_ld1##L##END##_v_tlb,             \
            clear_vertical_##L, copy_vertical_##L);                        \
}                                                                          \
void HELPER(sme_ld1##L##END##_h_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint32_t desc)     \
{                                                                          \
    sme_ld1_mte(env, za, vg, addr, desc, GETPC(), ESZ, false,              \
                sve_ld1##L##L##END##_host, sve_ld1##L##L##END##_tlb,       \
                clear_horizontal, copy_horizontal);                        \
}                                                                          \
void HELPER(sme_ld1##L##END##_v_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint32_t desc)     \
{                                                                          \
    sme_ld1_mte(env, za, vg, addr, desc, GETPC(), ESZ, true,               \
                sme_ld1##L##END##_v_host, sme_ld1##L##END##_v_tlb,         \
                clear_vertical_##L, copy_vertical_##L);                    \
}

DO_LD(b, , MO_8)
DO_LD(h, _be, MO_16)
DO_LD(h, _le, MO_16)
DO_LD(s, _be, MO_32)
DO_LD(s, _le, MO_32)
DO_LD(d, _be, MO_64)
DO_LD(d, _le, MO_64)
DO_LD(q, _be, MO_128)
DO_LD(q, _le, MO_128)

#undef DO_LD

/*
 * Common helper for all contiguous predicated stores.
 */

static inline QEMU_ALWAYS_INLINE
void sme_st1(CPUARMState *env, void *za, uint64_t *vg,
             const target_ulong addr, uint32_t desc, const uintptr_t ra,
             const int esz, uint32_t mtedesc, bool vertical,
             sve_ldst1_host_fn *host_fn,
             sve_ldst1_tlb_fn *tlb_fn)
{
    const intptr_t reg_max = simd_oprsz(desc);
    const intptr_t esize = 1 << esz;
    intptr_t reg_off, reg_last;
    SVEContLdSt info;
    void *host;
    int flags;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, esize)) {
        /* The entire predicate was false; no store occurs.  */
        return;
    }

    /* Probe the page(s).  Exit with exception for any invalid page. */
    sve_cont_ldst_pages(&info, FAULT_ALL, env, addr, MMU_DATA_STORE, ra);

    /* Handle watchpoints for all active elements. */
    sve_cont_ldst_watchpoints(&info, env, vg, addr, esize, esize,
                              BP_MEM_WRITE, ra);

    /*
     * Handle mte checks for all active elements.
     * Since TBI must be set for MTE, !mtedesc => !mte_active.
     */
    if (mtedesc) {
        sve_cont_ldst_mte_check(&info, env, vg, addr, esize, esize,
                                mtedesc, ra);
    }

    flags = info.page[0].flags | info.page[1].flags;
    if (unlikely(flags != 0)) {
#ifdef CONFIG_USER_ONLY
        g_assert_not_reached();
#else
        /*
         * At least one page includes MMIO.
         * Any bus operation can fail with cpu_transaction_failed,
         * which for ARM will raise SyncExternal.  We cannot avoid
         * this fault and will leave with the store incomplete.
         */
        reg_off = info.reg_off_first[0];
        reg_last = info.reg_off_last[1];
        if (reg_last < 0) {
            reg_last = info.reg_off_split;
            if (reg_last < 0) {
                reg_last = info.reg_off_last[0];
            }
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    tlb_fn(env, za, reg_off, addr + reg_off, ra);
                }
                reg_off += esize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
        return;
#endif
    }

    reg_off = info.reg_off_first[0];
    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    while (reg_off <= reg_last) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                host_fn(za, reg_off, host + reg_off);
            }
            reg_off += 1 << esz;
        } while (reg_off <= reg_last && (reg_off & 63));
    }

    /*
     * Use the slow path to manage the cross-page misalignment.
     * But we know this is RAM and cannot trap.
     */
    reg_off = info.reg_off_split;
    if (unlikely(reg_off >= 0)) {
        tlb_fn(env, za, reg_off, addr + reg_off, ra);
    }

    reg_off = info.reg_off_first[1];
    if (unlikely(reg_off >= 0)) {
        reg_last = info.reg_off_last[1];
        host = info.page[1].host;

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    host_fn(za, reg_off, host + reg_off);
                }
                reg_off += 1 << esz;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
    }
}

static inline QEMU_ALWAYS_INLINE
void sme_st1_mte(CPUARMState *env, void *za, uint64_t *vg, target_ulong addr,
                 uint32_t desc, uintptr_t ra, int esz, bool vertical,
                 sve_ldst1_host_fn *host_fn,
                 sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    int bit55 = extract64(addr, 55, 1);

    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(desc, bit55) ||
        tcma_check(desc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sme_st1(env, za, vg, addr, desc, ra, esz, mtedesc,
            vertical, host_fn, tlb_fn);
}

#define DO_ST(L, END, ESZ)                                                 \
void HELPER(sme_st1##L##END##_h)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint32_t desc)         \
{                                                                          \
    sme_st1(env, za, vg, addr, desc, GETPC(), ESZ, 0, false,               \
            sve_st1##L##L##END##_host, sve_st1##L##L##END##_tlb);          \
}                                                                          \
void HELPER(sme_st1##L##END##_v)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint32_t desc)         \
{                                                                          \
    sme_st1(env, za, vg, addr, desc, GETPC(), ESZ, 0, true,                \
            sme_st1##L##END##_v_host, sme_st1##L##END##_v_tlb);            \
}                                                                          \
void HELPER(sme_st1##L##END##_h_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint32_t desc)     \
{                                                                          \
    sme_st1_mte(env, za, vg, addr, desc, GETPC(), ESZ, false,              \
                sve_st1##L##L##END##_host, sve_st1##L##L##END##_tlb);      \
}                                                                          \
void HELPER(sme_st1##L##END##_v_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint32_t desc)     \
{                                                                          \
    sme_st1_mte(env, za, vg, addr, desc, GETPC(), ESZ, true,               \
                sme_st1##L##END##_v_host, sme_st1##L##END##_v_tlb);        \
}

DO_ST(b, , MO_8)
DO_ST(h, _be, MO_16)
DO_ST(h, _le, MO_16)
DO_ST(s, _be, MO_32)
DO_ST(s, _le, MO_32)
DO_ST(d, _be, MO_64)
DO_ST(d, _le, MO_64)
DO_ST(q, _be, MO_128)
DO_ST(q, _le, MO_128)

#undef DO_ST

void HELPER(sme_addha_s)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 4;
    uint64_t *pn = vpn, *pm = vpm;
    uint32_t * restrict zda = vzda, * restrict zn = vzn;

    for (row = 0; row < oprsz; ) {
        uint64_t pa = pn[row >> 4];
        do {
            if (pa & 1) {
                for (col = 0; col < oprsz; ) {
                    uint64_t pb = pm[col >> 4];
                    do {
                        if (pb & 1) {
                            zda[row * sizeof(ARMVectorReg) + col] += zn[col];
                        }
                        pb >>= 4;
                    } while (++col & 15);
                }
            }
            pa >>= 4;
        } while (++row & 15);
    }
}

void HELPER(sme_addha_d)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pn = vpn, *pm = vpm;
    uint64_t * restrict zda = vzda, * restrict zn = vzn;

    for (row = 0; row < oprsz; ++row) {
        if (pn[H1(row)] & 1) {
            for (col = 0; col < oprsz; ++col) {
                if (pm[H1(col)] & 1) {
                    zda[row * sizeof(ARMVectorReg) + col] += zn[col];
                }
            }
        }
    }
}

void HELPER(sme_addva_s)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 4;
    uint64_t *pn = vpn, *pm = vpm;
    uint32_t * restrict zda = vzda, * restrict zn = vzn;

    for (row = 0; row < oprsz; ) {
        uint64_t pa = pn[row >> 4];
        do {
            if (pa & 1) {
                uint32_t zn_row = zn[row];
                for (col = 0; col < oprsz; ) {
                    uint64_t pb = pm[col >> 4];
                    do {
                        if (pb & 1) {
                            zda[row * sizeof(ARMVectorReg) + col] += zn_row;
                        }
                        pb >>= 4;
                    } while (++col & 15);
                }
            }
            pa >>= 4;
        } while (++row & 15);
    }
}

void HELPER(sme_addva_d)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pn = vpn, *pm = vpm;
    uint64_t * restrict zda = vzda, * restrict zn = vzn;

    for (row = 0; row < oprsz; ++row) {
        if (pn[H1(row)] & 1) {
            uint64_t zn_row = zn[row];
            for (col = 0; col < oprsz; ++col) {
                if (pm[H1(col)] & 1) {
                    zda[row * sizeof(ARMVectorReg) + col] += zn_row;
                }
            }
        }
    }
}
