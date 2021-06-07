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
