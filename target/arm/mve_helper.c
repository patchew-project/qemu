/*
 * M-profile MVE Operations
 *
 * Copyright (c) 2021 Linaro, Ltd.
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
#include "qemu/int128.h"
#include "cpu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

/*
 * Note that vector data is stored in host-endian 64-bit chunks,
 * so addressing units smaller than that needs a host-endian fixup.
 */
#ifdef HOST_WORDS_BIGENDIAN
#define H1(x)  ((x) ^ 7)
#define H2(x)  ((x) ^ 3)
#define H4(x)  ((x) ^ 1)
#else
#define H1(x)  (x)
#define H2(x)  (x)
#define H4(x)  (x)
#endif

static uint16_t mve_element_mask(CPUARMState *env)
{
    /*
     * Return the mask of which elements in the MVE vector should be
     * updated. This is a combination of multiple things:
     *  (1) by default, we update every lane in the vector
     *  (2) VPT predication stores its state in the VPR register;
     *  (3) low-overhead-branch tail predication will mask out part
     *      the vector on the final iteration of the loop
     *  (4) if EPSR.ECI is set then we must execute only some beats
     *      of the insn
     * We combine all these into a 16-bit result with the same semantics
     * as VPR.P0: 0 to mask the lane, 1 if it is active.
     * 8-bit vector ops will look at all bits of the result;
     * 16-bit ops will look at bits 0, 2, 4, ...;
     * 32-bit ops will look at bits 0, 4, 8 and 12.
     * Compare pseudocode GetCurInstrBeat(), though that only returns
     * the 4-bit slice of the mask corresponding to a single beat.
     */
    uint16_t mask = extract32(env->v7m.vpr, R_V7M_VPR_P0_SHIFT,
                              R_V7M_VPR_P0_LENGTH);

    if (!(env->v7m.vpr & R_V7M_VPR_MASK01_MASK)) {
        mask |= 0xff;
    }
    if (!(env->v7m.vpr & R_V7M_VPR_MASK23_MASK)) {
        mask |= 0xff00;
    }

    if (env->v7m.ltpsize < 4 &&
        env->regs[14] <= (1 << (4 - env->v7m.ltpsize))) {
        /*
         * Tail predication active, and this is the last loop iteration.
         * The element size is (1 << ltpsize), and we only want to process
         * loopcount elements, so we want to retain the least significant
         * (loopcount * esize) predicate bits and zero out bits above that.
         */
        int masklen = env->regs[14] << env->v7m.ltpsize;
        assert(masklen <= 16);
        mask &= MAKE_64BIT_MASK(0, masklen);
    }

    if ((env->condexec_bits & 0xf) == 0) {
        /*
         * ECI bits indicate which beats are already executed;
         * we handle this by effectively predicating them out.
         */
        int eci = env->condexec_bits >> 4;
        switch (eci) {
        case ECI_NONE:
            break;
        case ECI_A0:
            mask &= 0xfff0;
            break;
        case ECI_A0A1:
            mask &= 0xff00;
            break;
        case ECI_A0A1A2:
        case ECI_A0A1A2B0:
            mask &= 0xf000;
            break;
        default:
            g_assert_not_reached();
        }
    }

    return mask;
}

static void mve_advance_vpt(CPUARMState *env)
{
    /* Advance the VPT and ECI state if necessary */
    uint32_t vpr = env->v7m.vpr;
    unsigned mask01, mask23;

    if ((env->condexec_bits & 0xf) == 0) {
        env->condexec_bits = (env->condexec_bits == (ECI_A0A1A2B0 << 4)) ?
            (ECI_A0 << 4) : (ECI_NONE << 4);
    }

    if (!(vpr & (R_V7M_VPR_MASK01_MASK | R_V7M_VPR_MASK23_MASK))) {
        /* VPT not enabled, nothing to do */
        return;
    }

    mask01 = extract32(vpr, R_V7M_VPR_MASK01_SHIFT, R_V7M_VPR_MASK01_LENGTH);
    mask23 = extract32(vpr, R_V7M_VPR_MASK23_SHIFT, R_V7M_VPR_MASK23_LENGTH);
    if (mask01 > 8) {
        /* high bit set, but not 0b1000: invert the relevant half of P0 */
        vpr ^= 0xff;
    }
    if (mask23 > 8) {
        /* high bit set, but not 0b1000: invert the relevant half of P0 */
        vpr ^= 0xff00;
    }
    vpr = deposit32(vpr, R_V7M_VPR_MASK01_SHIFT, R_V7M_VPR_MASK01_LENGTH,
                    mask01 << 1);
    vpr = deposit32(vpr, R_V7M_VPR_MASK23_SHIFT, R_V7M_VPR_MASK23_LENGTH,
                    mask23 << 1);
    env->v7m.vpr = vpr;
}


#define DO_VLDR(OP, ESIZE, LDTYPE, TYPE, H)                             \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, uint32_t addr)    \
    {                                                                   \
        TYPE *d = vd;                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned b, e;                                                  \
        /*                                                              \
         * R_SXTM allows the dest reg to become UNKNOWN for abandoned   \
         * beats so we don't care if we update part of the dest and     \
         * then take an exception.                                      \
         */                                                             \
        for (b = 0, e = 0; b < 16; b += ESIZE, e++) {                   \
            if (mask & (1 << b)) {                                      \
                d[H(e)] = cpu_##LDTYPE##_data_ra(env, addr, GETPC());   \
                addr += ESIZE;                                          \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

#define DO_VSTR(OP, ESIZE, STTYPE, TYPE, H)                             \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, uint32_t addr)    \
    {                                                                   \
        TYPE *d = vd;                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned b, e;                                                  \
        for (b = 0, e = 0; b < 16; b += ESIZE, e++) {                   \
            if (mask & (1 << b)) {                                      \
                cpu_##STTYPE##_data_ra(env, addr, d[H(e)], GETPC());    \
                addr += ESIZE;                                          \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

DO_VLDR(vldrb, 1, ldub, uint8_t, H1)
DO_VLDR(vldrh, 2, lduw, uint16_t, H2)
DO_VLDR(vldrw, 4, ldl, uint32_t, H4)

DO_VLDR(vldrb_sh, 2, ldsb, int16_t, H2)
DO_VLDR(vldrb_sw, 4, ldsb, int32_t, H4)
DO_VLDR(vldrb_uh, 2, ldub, uint16_t, H2)
DO_VLDR(vldrb_uw, 4, ldub, uint32_t, H4)
DO_VLDR(vldrh_sw, 4, ldsw, int32_t, H4)
DO_VLDR(vldrh_uw, 4, lduw, uint32_t, H4)

DO_VSTR(vstrb, 1, stb, uint8_t, H1)
DO_VSTR(vstrh, 2, stw, uint16_t, H2)
DO_VSTR(vstrw, 4, stl, uint32_t, H4)
DO_VSTR(vstrb_h, 2, stb, int16_t, H2)
DO_VSTR(vstrb_w, 4, stb, int32_t, H4)
DO_VSTR(vstrh_w, 4, stw, int32_t, H4)

#undef DO_VLDR
#undef DO_VSTR

/*
 * Take the bottom bits of mask (which is 1 bit per lane) and
 * convert to a mask which has 1s in each byte which is predicated.
 */
static uint8_t mask_to_bytemask1(uint16_t mask)
{
    return (mask & 1) ? 0xff : 0;
}

static uint16_t mask_to_bytemask2(uint16_t mask)
{
    static const uint16_t masks[] = { 0x0000, 0x00ff, 0xff00, 0xffff };
    return masks[mask & 3];
}

static uint32_t mask_to_bytemask4(uint16_t mask)
{
    static const uint32_t masks[] = {
        0x00000000, 0x000000ff, 0x0000ff00, 0x0000ffff,
        0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00ffffff,
        0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff,
        0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff,
    };
    return masks[mask & 0xf];
}

static uint64_t mask_to_bytemask8(uint16_t mask)
{
    return mask_to_bytemask4(mask) |
        ((uint64_t)mask_to_bytemask4(mask >> 4) << 32);
}

#define DO_VDUP(OP, ESIZE, TYPE, H)                                     \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, uint32_t val)     \
    {                                                                   \
        TYPE *d = vd;                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            uint64_t bytemask = mask_to_bytemask##ESIZE(mask);          \
            d[H(e)] &= ~bytemask;                                       \
            d[H(e)] |= (val & bytemask);                                \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

DO_VDUP(vdupb, 1, uint8_t, H1)
DO_VDUP(vduph, 2, uint16_t, H2)
DO_VDUP(vdupw, 4, uint32_t, H4)

#define DO_1OP(OP, ESIZE, TYPE, H, FN)                                  \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, void *vm)         \
    {                                                                   \
        TYPE *d = vd, *m = vm;                                          \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            TYPE r = FN(m[H(e)]);                                       \
            uint64_t bytemask = mask_to_bytemask##ESIZE(mask);          \
            d[H(e)] &= ~bytemask;                                       \
            d[H(e)] |= (r & bytemask);                                  \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

#define DO_CLS_B(N)   (clrsb32(N) - 24)
#define DO_CLS_H(N)   (clrsb32(N) - 16)

DO_1OP(vclsb, 1, int8_t, H1, DO_CLS_B)
DO_1OP(vclsh, 2, int16_t, H2, DO_CLS_H)
DO_1OP(vclsw, 4, int32_t, H4, clrsb32)

#define DO_CLZ_B(N)   (clz32(N) - 24)
#define DO_CLZ_H(N)   (clz32(N) - 16)

DO_1OP(vclzb, 1, uint8_t, H1, DO_CLZ_B)
DO_1OP(vclzh, 2, uint16_t, H2, DO_CLZ_H)
DO_1OP(vclzw, 4, uint32_t, H4, clz32)

DO_1OP(vrev16b, 2, uint16_t, H2, bswap16)
DO_1OP(vrev32b, 4, uint32_t, H4, bswap32)
DO_1OP(vrev32h, 4, uint32_t, H4, hswap32)
DO_1OP(vrev64b, 8, uint64_t, , bswap64)
DO_1OP(vrev64h, 8, uint64_t, , hswap64)
DO_1OP(vrev64w, 8, uint64_t, , wswap64)

#define DO_NOT(N) (~(N))

DO_1OP(vmvn, 1, uint8_t, H1, DO_NOT)

#define DO_ABS(N) ((N) < 0 ? -(N) : (N))
#define DO_FABS(N)    (N & ((__typeof(N))-1 >> 1))

DO_1OP(vabsb, 1, int8_t, H1, DO_ABS)
DO_1OP(vabsh, 2, int16_t, H2, DO_ABS)
DO_1OP(vabsw, 4, int32_t, H4, DO_ABS)

DO_1OP(vfabsh, 2, uint16_t, H2, DO_FABS)
DO_1OP(vfabss, 4, uint32_t, H4, DO_FABS)

#define DO_NEG(N)    (-(N))
#define DO_FNEG(N)    ((N) ^ ~((__typeof(N))-1 >> 1))

DO_1OP(vnegb, 1, int8_t, H1, DO_NEG)
DO_1OP(vnegh, 2, int16_t, H2, DO_NEG)
DO_1OP(vnegw, 4, int32_t, H4, DO_NEG)

DO_1OP(vfnegh, 2, uint16_t, H2, DO_FNEG)
DO_1OP(vfnegs, 4, uint32_t, H4, DO_FNEG)

#define DO_2OP(OP, ESIZE, TYPE, H, FN)                                  \
    void HELPER(glue(mve_, OP))(CPUARMState *env,                       \
                                void *vd, void *vn, void *vm)           \
    {                                                                   \
        TYPE *d = vd, *n = vn, *m = vm;                                 \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            TYPE r = FN(n[H(e)], m[H(e)]);                              \
            uint64_t bytemask = mask_to_bytemask##ESIZE(mask);          \
            d[H(e)] &= ~bytemask;                                       \
            d[H(e)] |= (r & bytemask);                                  \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

/* provide unsigned 2-op helpers for all sizes */
#define DO_2OP_U(OP, FN)                        \
    DO_2OP(OP##b, 1, uint8_t, H1, FN)           \
    DO_2OP(OP##h, 2, uint16_t, H2, FN)          \
    DO_2OP(OP##w, 4, uint32_t, H4, FN)

/* provide signed 2-op helpers for all sizes */
#define DO_2OP_S(OP, FN)                        \
    DO_2OP(OP##b, 1, int8_t, H1, FN)            \
    DO_2OP(OP##h, 2, int16_t, H2, FN)           \
    DO_2OP(OP##w, 4, int32_t, H4, FN)

/*
 * "Long" operations where two half-sized inputs (taken from either the
 * top or the bottom of the input vector) produce a double-width result.
 * Here TYPE and H are for the input, and LESIZE, LTYPE, LH for the output.
 */
#define DO_2OP_L(OP, TOP, TYPE, H, LESIZE, LTYPE, LH, FN)               \
    void HELPER(glue(mve_, OP))(CPUARMState *env, void *vd, void *vn, void *vm) \
    {                                                                   \
        LTYPE *d = vd;                                                  \
        TYPE *n = vn, *m = vm;                                          \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned le;                                                    \
        for (le = 0; le < 16 / LESIZE; le++, mask >>= LESIZE) {         \
            LTYPE r = FN((LTYPE)n[H(le * 2 + TOP)], m[H(le * 2 + TOP)]); \
            uint64_t bytemask = mask_to_bytemask##LESIZE(mask);         \
            d[LH(le)] &= ~bytemask;                                     \
            d[LH(le)] |= (r & bytemask);                                \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

#define DO_2OP_SAT(OP, ESIZE, TYPE, H, FN)                              \
    void HELPER(glue(mve_, OP))(CPUARMState *env, void *vd, void *vn, void *vm) \
    {                                                                   \
        TYPE *d = vd, *n = vn, *m = vm;                                 \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            bool sat = false;                                           \
            TYPE r = FN(n[H(e)], m[H(e)], &sat);                        \
            uint64_t bytemask = mask_to_bytemask##ESIZE(mask);          \
            d[H(e)] &= ~bytemask;                                       \
            d[H(e)] |= (r & bytemask);                                  \
            if (sat && (mask & 1)) {                                    \
                env->vfp.qc[0] = 1;                                     \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

/* provide unsigned 2-op helpers for all sizes */
#define DO_2OP_SAT_U(OP, FN)                    \
    DO_2OP_SAT(OP##b, 1, uint8_t, H1, FN)       \
    DO_2OP_SAT(OP##h, 2, uint16_t, H2, FN)      \
    DO_2OP_SAT(OP##w, 4, uint32_t, H4, FN)

/* provide signed 2-op helpers for all sizes */
#define DO_2OP_SAT_S(OP, FN)                    \
    DO_2OP_SAT(OP##b, 1, int8_t, H1, FN)        \
    DO_2OP_SAT(OP##h, 2, int16_t, H2, FN)       \
    DO_2OP_SAT(OP##w, 4, int32_t, H4, FN)

#define DO_AND(N, M)  ((N) & (M))
#define DO_BIC(N, M)  ((N) & ~(M))
#define DO_ORR(N, M)  ((N) | (M))
#define DO_ORN(N, M)  ((N) | ~(M))
#define DO_EOR(N, M)  ((N) ^ (M))

DO_2OP(vand, 1, uint8_t, H1, DO_AND)
DO_2OP(vbic, 1, uint8_t, H1, DO_BIC)
DO_2OP(vorr, 1, uint8_t, H1, DO_ORR)
DO_2OP(vorn, 1, uint8_t, H1, DO_ORN)
DO_2OP(veor, 1, uint8_t, H1, DO_EOR)

#define DO_ADD(N, M) ((N) + (M))
#define DO_SUB(N, M) ((N) - (M))
#define DO_MUL(N, M) ((N) * (M))

DO_2OP_U(vadd, DO_ADD)
DO_2OP_U(vsub, DO_SUB)
DO_2OP_U(vmul, DO_MUL)

DO_2OP_L(vmullbsb, 0, int8_t, H1, 2, int16_t, H2, DO_MUL)
DO_2OP_L(vmullbsh, 0, int16_t, H2, 4, int32_t, H4, DO_MUL)
DO_2OP_L(vmullbsw, 0, int32_t, H4, 8, int64_t, , DO_MUL)
DO_2OP_L(vmullbub, 0, uint8_t, H1, 2, uint16_t, H2, DO_MUL)
DO_2OP_L(vmullbuh, 0, uint16_t, H2, 4, uint32_t, H4, DO_MUL)
DO_2OP_L(vmullbuw, 0, uint32_t, H4, 8, uint64_t, , DO_MUL)

DO_2OP_L(vmulltsb, 1, int8_t, H1, 2, int16_t, H2, DO_MUL)
DO_2OP_L(vmulltsh, 1, int16_t, H2, 4, int32_t, H4, DO_MUL)
DO_2OP_L(vmulltsw, 1, int32_t, H4, 8, int64_t, , DO_MUL)
DO_2OP_L(vmulltub, 1, uint8_t, H1, 2, uint16_t, H2, DO_MUL)
DO_2OP_L(vmulltuh, 1, uint16_t, H2, 4, uint32_t, H4, DO_MUL)
DO_2OP_L(vmulltuw, 1, uint32_t, H4, 8, uint64_t, , DO_MUL)

/*
 * Because the computation type is at least twice as large as required,
 * these work for both signed and unsigned source types.
 */
static inline uint8_t do_mulh_b(int32_t n, int32_t m)
{
    return (n * m) >> 8;
}

static inline uint16_t do_mulh_h(int32_t n, int32_t m)
{
    return (n * m) >> 16;
}

static inline uint32_t do_mulh_w(int64_t n, int64_t m)
{
    return (n * m) >> 32;
}

static inline uint8_t do_rmulh_b(int32_t n, int32_t m)
{
    return (n * m + (1U << 7)) >> 8;
}

static inline uint16_t do_rmulh_h(int32_t n, int32_t m)
{
    return (n * m + (1U << 15)) >> 16;
}

static inline uint32_t do_rmulh_w(int64_t n, int64_t m)
{
    return (n * m + (1U << 31)) >> 32;
}

DO_2OP(vmulhsb, 1, int8_t, H1, do_mulh_b)
DO_2OP(vmulhsh, 2, int16_t, H2, do_mulh_h)
DO_2OP(vmulhsw, 4, int32_t, H4, do_mulh_w)
DO_2OP(vmulhub, 1, uint8_t, H1, do_mulh_b)
DO_2OP(vmulhuh, 2, uint16_t, H2, do_mulh_h)
DO_2OP(vmulhuw, 4, uint32_t, H4, do_mulh_w)

DO_2OP(vrmulhsb, 1, int8_t, H1, do_rmulh_b)
DO_2OP(vrmulhsh, 2, int16_t, H2, do_rmulh_h)
DO_2OP(vrmulhsw, 4, int32_t, H4, do_rmulh_w)
DO_2OP(vrmulhub, 1, uint8_t, H1, do_rmulh_b)
DO_2OP(vrmulhuh, 2, uint16_t, H2, do_rmulh_h)
DO_2OP(vrmulhuw, 4, uint32_t, H4, do_rmulh_w)

#define DO_MAX(N, M)  ((N) >= (M) ? (N) : (M))
#define DO_MIN(N, M)  ((N) >= (M) ? (M) : (N))

DO_2OP_S(vmaxs, DO_MAX)
DO_2OP_U(vmaxu, DO_MAX)
DO_2OP_S(vmins, DO_MIN)
DO_2OP_U(vminu, DO_MIN)

#define DO_ABD(N, M)  ((N) >= (M) ? (N) - (M) : (M) - (N))

DO_2OP_S(vabds, DO_ABD)
DO_2OP_U(vabdu, DO_ABD)

static inline uint32_t do_vhadd_u(uint32_t n, uint32_t m)
{
    return ((uint64_t)n + m) >> 1;
}

static inline int32_t do_vhadd_s(int32_t n, int32_t m)
{
    return ((int64_t)n + m) >> 1;
}

static inline uint32_t do_vhsub_u(uint32_t n, uint32_t m)
{
    return ((uint64_t)n - m) >> 1;
}

static inline int32_t do_vhsub_s(int32_t n, int32_t m)
{
    return ((int64_t)n - m) >> 1;
}

DO_2OP_S(vhadds, do_vhadd_s)
DO_2OP_U(vhaddu, do_vhadd_u)
DO_2OP_S(vhsubs, do_vhsub_s)
DO_2OP_U(vhsubu, do_vhsub_u)

static inline int32_t do_sat_bhw(int64_t val, int64_t min, int64_t max, bool *s)
{
    if (val > max) {
        *s = true;
        return max;
    } else if (val < min) {
        *s = true;
        return min;
    }
    return val;
}

#define DO_SQADD_B(n, m, s) do_sat_bhw((int64_t)n + m, INT8_MIN, INT8_MAX, s)
#define DO_SQADD_H(n, m, s) do_sat_bhw((int64_t)n + m, INT16_MIN, INT16_MAX, s)
#define DO_SQADD_W(n, m, s) do_sat_bhw((int64_t)n + m, INT32_MIN, INT32_MAX, s)

#define DO_UQADD_B(n, m, s) do_sat_bhw((int64_t)n + m, 0, UINT8_MAX, s)
#define DO_UQADD_H(n, m, s) do_sat_bhw((int64_t)n + m, 0, UINT16_MAX, s)
#define DO_UQADD_W(n, m, s) do_sat_bhw((int64_t)n + m, 0, UINT32_MAX, s)

#define DO_SQSUB_B(n, m, s) do_sat_bhw((int64_t)n - m, INT8_MIN, INT8_MAX, s)
#define DO_SQSUB_H(n, m, s) do_sat_bhw((int64_t)n - m, INT16_MIN, INT16_MAX, s)
#define DO_SQSUB_W(n, m, s) do_sat_bhw((int64_t)n - m, INT32_MIN, INT32_MAX, s)

#define DO_UQSUB_B(n, m, s) do_sat_bhw((int64_t)n - m, 0, UINT8_MAX, s)
#define DO_UQSUB_H(n, m, s) do_sat_bhw((int64_t)n - m, 0, UINT16_MAX, s)
#define DO_UQSUB_W(n, m, s) do_sat_bhw((int64_t)n - m, 0, UINT32_MAX, s)

/*
 * For QDMULH and QRDMULH we simplify "double and shift by esize" into
 * "shift by esize-1", adjusting the QRDMULH rounding constant to match.
 */
#define DO_QDMULH_B(n, m, s) do_sat_bhw(((int64_t)n * m) >> 7, \
                                        INT8_MIN, INT8_MAX, s)
#define DO_QDMULH_H(n, m, s) do_sat_bhw(((int64_t)n * m) >> 15, \
                                        INT16_MIN, INT16_MAX, s)
#define DO_QDMULH_W(n, m, s) do_sat_bhw(((int64_t)n * m) >> 31, \
                                        INT32_MIN, INT32_MAX, s)

#define DO_QRDMULH_B(n, m, s) do_sat_bhw(((int64_t)n * m + (1 << 6)) >> 7, \
                                         INT8_MIN, INT8_MAX, s)
#define DO_QRDMULH_H(n, m, s) do_sat_bhw(((int64_t)n * m + (1 << 14)) >> 15, \
                                         INT16_MIN, INT16_MAX, s)
#define DO_QRDMULH_W(n, m, s) do_sat_bhw(((int64_t)n * m + (1 << 30)) >> 31, \
                                         INT32_MIN, INT32_MAX, s)

DO_2OP_SAT(vqdmulhb, 1, int8_t, H1, DO_QDMULH_B)
DO_2OP_SAT(vqdmulhh, 2, int16_t, H2, DO_QDMULH_H)
DO_2OP_SAT(vqdmulhw, 4, int32_t, H4, DO_QDMULH_W)

DO_2OP_SAT(vqrdmulhb, 1, int8_t, H1, DO_QRDMULH_B)
DO_2OP_SAT(vqrdmulhh, 2, int16_t, H2, DO_QRDMULH_H)
DO_2OP_SAT(vqrdmulhw, 4, int32_t, H4, DO_QRDMULH_W)

DO_2OP_SAT(vqaddub, 1, uint8_t, H1, DO_UQADD_B)
DO_2OP_SAT(vqadduh, 2, uint16_t, H2, DO_UQADD_H)
DO_2OP_SAT(vqadduw, 4, uint32_t, H4, DO_UQADD_W)
DO_2OP_SAT(vqaddsb, 1, int8_t, H1, DO_SQADD_B)
DO_2OP_SAT(vqaddsh, 2, int16_t, H2, DO_SQADD_H)
DO_2OP_SAT(vqaddsw, 4, int32_t, H4, DO_SQADD_W)

DO_2OP_SAT(vqsubub, 1, uint8_t, H1, DO_UQSUB_B)
DO_2OP_SAT(vqsubuh, 2, uint16_t, H2, DO_UQSUB_H)
DO_2OP_SAT(vqsubuw, 4, uint32_t, H4, DO_UQSUB_W)
DO_2OP_SAT(vqsubsb, 1, int8_t, H1, DO_SQSUB_B)
DO_2OP_SAT(vqsubsh, 2, int16_t, H2, DO_SQSUB_H)
DO_2OP_SAT(vqsubsw, 4, int32_t, H4, DO_SQSUB_W)

#define DO_SQSHL_OP(src1, src2, satp)                           \
    ({                                                          \
        int8_t tmp;                                             \
        typeof(src1) dest;                                      \
        tmp = (int8_t)src2;                                     \
        if (tmp >= (ssize_t)sizeof(src1) * 8) {                 \
            if (src1) {                                         \
                *satp = true;                                   \
                dest = (uint32_t)(1 << (sizeof(src1) * 8 - 1)); \
                if (src1 > 0) {                                 \
                    dest--;                                     \
                }                                               \
            } else {                                            \
                dest = src1;                                    \
            }                                                   \
        } else if (tmp <= -(ssize_t)sizeof(src1) * 8) {         \
            dest = src1 >> 31;                                  \
        } else if (tmp < 0) {                                   \
            dest = src1 >> -tmp;                                \
        } else {                                                \
            dest = src1 << tmp;                                 \
            if ((dest >> tmp) != src1) {                        \
                *satp = true;                                   \
                dest = (uint32_t)(1 << (sizeof(src1) * 8 - 1)); \
                if (src1 > 0) {                                 \
                    dest--;                                     \
                }                                               \
            }                                                   \
        }                                                       \
        dest;                                                   \
    })

#define DO_UQSHL_OP(src1, src2, satp)                   \
    ({                                                  \
        int8_t tmp;                                     \
        typeof(src1) dest;                              \
        tmp = (int8_t)src2;                             \
        if (tmp >= (ssize_t)sizeof(src1) * 8) {         \
            if (src1) {                                 \
                *satp = true;                           \
                dest = ~0;                              \
            } else {                                    \
                dest = 0;                               \
            }                                           \
        } else if (tmp <= -(ssize_t)sizeof(src1) * 8) { \
            dest = 0;                                   \
        } else if (tmp < 0) {                           \
            dest = src1 >> -tmp;                        \
        } else {                                        \
            dest = src1 << tmp;                         \
            if ((dest >> tmp) != src1) {                \
                *satp = true;                           \
                dest = ~0;                              \
            }                                           \
        }                                               \
        dest;                                           \
    })

DO_2OP_SAT_S(vqshls, DO_SQSHL_OP)
DO_2OP_SAT_U(vqshlu, DO_UQSHL_OP)

#define DO_UQRSHL_OP(src1, src2, satp)                  \
    ({                                                  \
        int8_t tmp;                                     \
        typeof(src1) dest;                              \
        tmp = (int8_t)src2;                             \
        if (tmp >= (ssize_t)sizeof(src1) * 8) {         \
            if (src1) {                                 \
                *satp = true;                           \
                dest = ~0;                              \
            } else {                                    \
                dest = 0;                               \
            }                                           \
        } else if (tmp < -(ssize_t)sizeof(src1) * 8) {  \
            dest = 0;                                   \
        } else if (tmp == -(ssize_t)sizeof(src1) * 8) { \
            dest = src1 >> (sizeof(src1) * 8 - 1);      \
        } else if (tmp < 0) {                           \
            dest = (src1 + (1 << (-1 - tmp))) >> -tmp;  \
        } else {                                        \
            dest = src1 << tmp;                         \
            if ((dest >> tmp) != src1) {                \
                *satp = true;                           \
                dest = ~0;                              \
            }                                           \
        }                                               \
        dest;                                           \
    })

/*
 * The addition of the rounding constant may overflow, so we use an
 * intermediate 64 bit accumulator for the 32-bit version.
 */
#define DO_UQRSHL32_OP(src1, src2, satp)                                \
    ({                                                                  \
        uint32_t dest;                                                  \
        uint32_t val = src1;                                            \
        int8_t shift = (int8_t)src2;                                    \
        if (shift >= 32) {                                              \
            if (val) {                                                  \
                *satp = true;                                           \
                dest = ~0;                                              \
            } else {                                                    \
                dest = 0;                                               \
            }                                                           \
        } else if (shift < -32) {                                       \
            dest = 0;                                                   \
        } else if (shift == -32) {                                      \
            dest = val >> 31;                                           \
        } else if (shift < 0) {                                         \
            uint64_t big_dest = ((uint64_t)val + (1 << (-1 - shift)));  \
            dest = big_dest >> -shift;                                  \
        } else {                                                        \
            dest = val << shift;                                        \
            if ((dest >> shift) != val) {                               \
                *satp = true;                                           \
                dest = ~0;                                              \
            }                                                           \
        }                                                               \
        dest;                                                           \
    })

#define DO_SQRSHL_OP(src1, src2, satp)                                  \
    ({                                                                  \
        int8_t tmp;                                                     \
        typeof(src1) dest;                                              \
        tmp = (int8_t)src2;                                             \
        if (tmp >= (ssize_t)sizeof(src1) * 8) {                         \
            if (src1) {                                                 \
                *satp = true;                                           \
                dest = (typeof(dest))(1 << (sizeof(src1) * 8 - 1));     \
                if (src1 > 0) {                                         \
                    dest--;                                             \
                }                                                       \
            } else {                                                    \
                dest = 0;                                               \
            }                                                           \
        } else if (tmp <= -(ssize_t)sizeof(src1) * 8) {                 \
            dest = 0;                                                   \
        } else if (tmp < 0) {                                           \
            dest = (src1 + (1 << (-1 - tmp))) >> -tmp;                  \
        } else {                                                        \
            dest = src1 << tmp;                                         \
            if ((dest >> tmp) != src1) {                                \
                *satp = true;                                           \
                dest = (uint32_t)(1 << (sizeof(src1) * 8 - 1));         \
                if (src1 > 0) {                                         \
                    dest--;                                             \
                }                                                       \
            }                                                           \
        }                                                               \
        dest;                                                           \
    })

#define DO_SQRSHL32_OP(src1, src2, satp)                                \
    ({                                                                  \
        int32_t dest;                                                   \
        int32_t val = (int32_t)src1;                                    \
        int8_t shift = (int8_t)src2;                                    \
        if (shift >= 32) {                                              \
            if (val) {                                                  \
                *satp = true;                                           \
                dest = (val >> 31) ^ ~(1U << 31);                       \
            } else {                                                    \
                dest = 0;                                               \
            }                                                           \
        } else if (shift <= -32) {                                      \
            dest = 0;                                                   \
        } else if (shift < 0) {                                         \
            int64_t big_dest = ((int64_t)val + (1 << (-1 - shift)));    \
            dest = big_dest >> -shift;                                  \
        } else {                                                        \
            dest = val << shift;                                        \
            if ((dest >> shift) != val) {                               \
                *satp = true;                                           \
                dest = (val >> 31) ^ ~(1U << 31);                       \
            }                                                           \
        }                                                               \
        dest;                                                           \
    })

DO_2OP_SAT(vqrshlub, 1, uint8_t, H1, DO_UQRSHL_OP)
DO_2OP_SAT(vqrshluh, 2, uint16_t, H2, DO_UQRSHL_OP)
DO_2OP_SAT(vqrshluw, 4, uint32_t, H4, DO_UQRSHL32_OP)
DO_2OP_SAT(vqrshlsb, 1, int8_t, H1, DO_SQRSHL_OP)
DO_2OP_SAT(vqrshlsh, 2, int16_t, H2, DO_SQRSHL_OP)
DO_2OP_SAT(vqrshlsw, 4, int32_t, H4, DO_SQRSHL32_OP)

#define DO_2OP_SCALAR(OP, ESIZE, TYPE, H, FN)                           \
    void HELPER(glue(mve_, OP))(CPUARMState *env, void *vd, void *vn,   \
                                uint32_t rm)                            \
    {                                                                   \
        TYPE *d = vd, *n = vn;                                          \
        TYPE m = rm;                                                    \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            TYPE r = FN(n[H(e)], m);                                    \
            uint64_t bytemask = mask_to_bytemask##ESIZE(mask);          \
            d[H(e)] &= ~bytemask;                                       \
            d[H(e)] |= (r & bytemask);                                  \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

#define DO_2OP_SAT_SCALAR(OP, ESIZE, TYPE, H, FN)                       \
    void HELPER(glue(mve_, OP))(CPUARMState *env, void *vd, void *vn,   \
                                uint32_t rm)                            \
    {                                                                   \
        TYPE *d = vd, *n = vn;                                          \
        TYPE m = rm;                                                    \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            bool sat = false;                                           \
            TYPE r = FN(n[H(e)], m, &sat);                              \
            uint64_t bytemask = mask_to_bytemask##ESIZE(mask);          \
            d[H(e)] &= ~bytemask;                                       \
            d[H(e)] |= (r & bytemask);                                  \
            if (sat && (mask & 1)) {                                    \
                env->vfp.qc[0] = 1;                                     \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

/* provide unsigned 2-op scalar helpers for all sizes */
#define DO_2OP_SCALAR_U(OP, FN)                 \
    DO_2OP_SCALAR(OP##b, 1, uint8_t, H1, FN)    \
    DO_2OP_SCALAR(OP##h, 2, uint16_t, H2, FN)   \
    DO_2OP_SCALAR(OP##w, 4, uint32_t, H4, FN)
#define DO_2OP_SCALAR_S(OP, FN)                 \
    DO_2OP_SCALAR(OP##b, 1, int8_t, H1, FN)     \
    DO_2OP_SCALAR(OP##h, 2, int16_t, H2, FN)    \
    DO_2OP_SCALAR(OP##w, 4, int32_t, H4, FN)

DO_2OP_SCALAR_U(vadd_scalar, DO_ADD)
DO_2OP_SCALAR_U(vsub_scalar, DO_SUB)
DO_2OP_SCALAR_U(vmul_scalar, DO_MUL)
DO_2OP_SCALAR_S(vhadds_scalar, do_vhadd_s)
DO_2OP_SCALAR_U(vhaddu_scalar, do_vhadd_u)
DO_2OP_SCALAR_S(vhsubs_scalar, do_vhsub_s)
DO_2OP_SCALAR_U(vhsubu_scalar, do_vhsub_u)

DO_2OP_SAT_SCALAR(vqaddu_scalarb, 1, uint8_t, H1, DO_UQADD_B)
DO_2OP_SAT_SCALAR(vqaddu_scalarh, 2, uint16_t, H2, DO_UQADD_H)
DO_2OP_SAT_SCALAR(vqaddu_scalarw, 4, uint32_t, H4, DO_UQADD_W)
DO_2OP_SAT_SCALAR(vqadds_scalarb, 1, int8_t, H1, DO_SQADD_B)
DO_2OP_SAT_SCALAR(vqadds_scalarh, 2, int16_t, H2, DO_SQADD_H)
DO_2OP_SAT_SCALAR(vqadds_scalarw, 4, int32_t, H4, DO_SQADD_W)

DO_2OP_SAT_SCALAR(vqsubu_scalarb, 1, uint8_t, H1, DO_UQSUB_B)
DO_2OP_SAT_SCALAR(vqsubu_scalarh, 2, uint16_t, H2, DO_UQSUB_H)
DO_2OP_SAT_SCALAR(vqsubu_scalarw, 4, uint32_t, H4, DO_UQSUB_W)
DO_2OP_SAT_SCALAR(vqsubs_scalarb, 1, int8_t, H1, DO_SQSUB_B)
DO_2OP_SAT_SCALAR(vqsubs_scalarh, 2, int16_t, H2, DO_SQSUB_H)
DO_2OP_SAT_SCALAR(vqsubs_scalarw, 4, int32_t, H4, DO_SQSUB_W)

DO_2OP_SAT_SCALAR(vqdmulh_scalarb, 1, int8_t, H1, DO_QDMULH_B)
DO_2OP_SAT_SCALAR(vqdmulh_scalarh, 2, int16_t, H2, DO_QDMULH_H)
DO_2OP_SAT_SCALAR(vqdmulh_scalarw, 4, int32_t, H4, DO_QDMULH_W)
DO_2OP_SAT_SCALAR(vqrdmulh_scalarb, 1, int8_t, H1, DO_QRDMULH_B)
DO_2OP_SAT_SCALAR(vqrdmulh_scalarh, 2, int16_t, H2, DO_QRDMULH_H)
DO_2OP_SAT_SCALAR(vqrdmulh_scalarw, 4, int32_t, H4, DO_QRDMULH_W)

/*
 * Long saturating scalar ops. As with DO_2OP_L, TYPE and H are for the
 * input (smaller) type and LESIZE, LTYPE, LH for the output (long) type.
 * SATMASK specifies which bits of the predicate mask matter for determining
 * whether to propagate a saturation indication into FPSCR.QC -- for
 * the 16x16->32 case we must check only the bit corresponding to the T or B
 * half that we used, but for the 32x32->64 case we propagate if the mask
 * bit is set for either half.
 */
#define DO_2OP_SAT_SCALAR_L(OP, TOP, TYPE, H, LESIZE, LTYPE, LH, FN, SATMASK) \
    void HELPER(glue(mve_, OP))(CPUARMState *env, void *vd, void *vn,   \
                                uint32_t rm)                            \
    {                                                                   \
        LTYPE *d = vd;                                                  \
        TYPE *n = vn;                                                   \
        TYPE m = rm;                                                    \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned le;                                                    \
        for (le = 0; le < 16 / LESIZE; le++, mask >>= LESIZE) {         \
            bool sat = false;                                           \
            LTYPE r = FN((LTYPE)n[H(le * 2 + TOP)], m, &sat);           \
            uint64_t bytemask = mask_to_bytemask##LESIZE(mask);         \
            d[LH(le)] &= ~bytemask;                                     \
            d[LH(le)] |= (r & bytemask);                                \
            if (sat && (mask & SATMASK)) {                              \
                env->vfp.qc[0] = 1;                                     \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

static inline int32_t do_qdmullh(int16_t n, int16_t m, bool *sat)
{
    int64_t r = ((int64_t)n * m) * 2;
    return do_sat_bhw(r, INT32_MIN, INT32_MAX, sat);
}

static inline int64_t do_qdmullw(int32_t n, int32_t m, bool *sat)
{
    /* The multiply can't overflow, but the doubling might */
    int64_t r = (int64_t)n * m;
    if (r > INT64_MAX / 2) {
        *sat = true;
        return INT64_MAX;
    } else if (r < INT64_MIN / 2) {
        *sat = true;
        return INT64_MIN;
    } else {
        return r * 2;
    }
}

#define SATMASK16B 1
#define SATMASK16T (1 << 2)
#define SATMASK32 ((1 << 4) | 1)

DO_2OP_SAT_SCALAR_L(vqdmullb_scalarh, 0, int16_t, H2, 4, int32_t, H4, \
                    do_qdmullh, SATMASK16B)
DO_2OP_SAT_SCALAR_L(vqdmullb_scalarw, 0, int32_t, H4, 8, int64_t, , \
                    do_qdmullw, SATMASK32)
DO_2OP_SAT_SCALAR_L(vqdmullt_scalarh, 1, int16_t, H2, 4, int32_t, H4, \
                    do_qdmullh, SATMASK16T)
DO_2OP_SAT_SCALAR_L(vqdmullt_scalarw, 1, int32_t, H4, 8, int64_t, , \
                    do_qdmullw, SATMASK32)

static inline uint32_t do_vbrsrb(uint32_t n, uint32_t m)
{
    m &= 0xff;
    if (m == 0) {
        return 0;
    }
    n = revbit8(n);
    if (m < 8) {
        n >>= 8 - m;
    }
    return n;
}

static inline uint32_t do_vbrsrh(uint32_t n, uint32_t m)
{
    m &= 0xff;
    if (m == 0) {
        return 0;
    }
    n = revbit16(n);
    if (m < 16) {
        n >>= 16 - m;
    }
    return n;
}

static inline uint32_t do_vbrsrw(uint32_t n, uint32_t m)
{
    m &= 0xff;
    if (m == 0) {
        return 0;
    }
    n = revbit32(n);
    if (m < 32) {
        n >>= 32 - m;
    }
    return n;
}

DO_2OP_SCALAR(vbrsrb, 1, uint8_t, H1, do_vbrsrb)
DO_2OP_SCALAR(vbrsrh, 2, uint16_t, H2, do_vbrsrh)
DO_2OP_SCALAR(vbrsrw, 4, uint32_t, H4, do_vbrsrw)

/*
 * Multiply add long dual accumulate ops.
 */
#define DO_LDAV(OP, ESIZE, TYPE, H, XCHG, EVENACC, ODDACC)              \
    uint64_t HELPER(glue(mve_, OP))(CPUARMState *env, void *vn,         \
                                    void *vm, uint64_t a)               \
    {                                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        TYPE *n = vn, *m = vm;                                          \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            if (mask & 1) {                                             \
                if (e & 1) {                                            \
                    a ODDACC (int64_t)n[H(e - 1 * XCHG)] * m[H(e)];     \
                } else {                                                \
                    a EVENACC (int64_t)n[H(e + 1 * XCHG)] * m[H(e)];    \
                }                                                       \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
        return a;                                                       \
    }

DO_LDAV(vmlaldavsh, 2, int16_t, H2, false, +=, +=)
DO_LDAV(vmlaldavxsh, 2, int16_t, H2, true, +=, +=)
DO_LDAV(vmlaldavsw, 4, int32_t, H4, false, +=, +=)
DO_LDAV(vmlaldavxsw, 4, int32_t, H4, true, +=, +=)

DO_LDAV(vmlaldavuh, 2, uint16_t, H2, false, +=, +=)
DO_LDAV(vmlaldavuw, 4, uint32_t, H4, false, +=, +=)

DO_LDAV(vmlsldavsh, 2, int16_t, H2, false, +=, -=)
DO_LDAV(vmlsldavxsh, 2, int16_t, H2, true, +=, -=)
DO_LDAV(vmlsldavsw, 4, int32_t, H4, false, +=, -=)
DO_LDAV(vmlsldavxsw, 4, int32_t, H4, true, +=, -=)

/*
 * Rounding multiply add long dual accumulate high: we must keep
 * a 72-bit internal accumulator value and return the top 64 bits.
 */
#define DO_LDAVH(OP, ESIZE, TYPE, H, XCHG, EVENACC, ODDACC, TO128)      \
    uint64_t HELPER(glue(mve_, OP))(CPUARMState *env, void *vn,         \
                                    void *vm, uint64_t a)               \
    {                                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        TYPE *n = vn, *m = vm;                                          \
        Int128 acc = TO128(a);                                          \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            if (mask & 1) {                                             \
                if (e & 1) {                                            \
                    acc = ODDACC(acc, TO128(n[H(e - 1 * XCHG)] * m[H(e)])); \
                } else {                                                \
                    acc = EVENACC(acc, TO128(n[H(e + 1 * XCHG)] * m[H(e)])); \
                }                                                       \
                acc = int128_add(acc, 1 << 7);                          \
            }                                                           \
        }                                                               \
        mve_advance_vpt(env);                                           \
        return int128_getlo(int128_rshift(acc, 8));                     \
    }

DO_LDAVH(vrmlaldavhsw, 4, int32_t, H4, false, int128_add, int128_add, int128_makes64)
DO_LDAVH(vrmlaldavhxsw, 4, int32_t, H4, true, int128_add, int128_add, int128_makes64)

DO_LDAVH(vrmlaldavhuw, 4, uint32_t, H4, false, int128_add, int128_add, int128_make64)

DO_LDAVH(vrmlsldavhsw, 4, int32_t, H4, false, int128_add, int128_sub, int128_makes64)
DO_LDAVH(vrmlsldavhxsw, 4, int32_t, H4, true, int128_add, int128_sub, int128_makes64)
